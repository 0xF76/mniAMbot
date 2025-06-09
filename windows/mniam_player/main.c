#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include "amcom.h"
#include "amcom_packets.h"

//additional includes for the player
#include <math.h>

#define MAX_NUMBER_OF_PLAYERS 8
#define MAX_NUMBER_OF_OBJECTS 255

#define DANGER_RAD 120.0f
#define SAFE_MARGIN 70.0f
#define THETA_MAX (M_PI/4)


enum AMCOM_ObjectType {
    AMCOM_OBJECT_PLAYER = 0,
    AMCOM_OBJECT_FOOD = 1,
    AMCOM_OBJECT_SPARK = 2,
    AMCOM_OBJECT_GLUE = 3
};

typedef struct AMPACKED {
    int8_t hp;
    float x;
    float y;
} object_t;

typedef struct {
    uint8_t my_player_number;
    uint8_t number_of_players;
    object_t players[MAX_NUMBER_OF_PLAYERS];

    object_t food[MAX_NUMBER_OF_OBJECTS];
    object_t sparks[MAX_NUMBER_OF_OBJECTS];
    object_t glue[MAX_NUMBER_OF_OBJECTS];


    float map_width;
    float map_height;
} game_t;

game_t game = {
    .my_player_number = 0,
    .number_of_players = 0,
    .map_width = 0.0f,
    .map_height = 0.0f
};

typedef struct {
    float x;
    float y;
} vec_t;

vec_t v_sub(vec_t a, vec_t b) {
    vec_t result;
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    return result;
}

float v_len(vec_t v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

vec_t v_norm(vec_t v) {
    float len = v_len(v);
    vec_t result;
    if(len > 1e-4f) {
        return (vec_t){v.x / len, v.y / len};
    } else {
        return (vec_t){0.0f, 0.0f}; // return zero vector if length is too small
    }
}

float wrap_signed(float angle) {
    while (angle < -M_PI) angle += 2.0f * M_PI;
    while (angle > M_PI) angle -= 2.0f * M_PI;
    return angle;
}

float wrap_unsigned(float angle) {
    while (angle < 0.0f) angle += 2.0f * M_PI;
    while (angle >= 2.0f * M_PI) angle -= 2.0f * M_PI;
    return angle;
}


// TODO: radius zalezny od rozmiaru mapy
// TODO: radius zalezny od rozmiaru gracza

float choose_angle(void) {
    /* Logika: ucieczka -> jedzenie -> polowanie -> reszta */

    // Poprzedni kierunek gracza
    static float heading = 0.0f;

    // Zbieranie informacji o planszy

    // zagrożenie
    float worst_danger_score = 0.0f;
    vec_t danger_vec = {0.0f, 0.0f};

    // jedzenie
    float best_food_dist = 1e9f;
    vec_t food_vec = {0.0f, 0.0f};

    // potencjalna ofiara
    float best_prey_dist = 1e9f;
    vec_t prey_vec = {0.0f, 0.0f};


    object_t* my_player = &game.players[game.my_player_number];


    /* Gracze */
    for(int i = 0; i < game.number_of_players; i++) {
        if(i == game.my_player_number) {
            continue; // pomiń samego siebie
        }

        object_t* player = &game.players[i];

        if(player->hp <= 0) {
            continue; // pomiń martwych graczy
        }

        vec_t d = v_sub((vec_t){player->x, player->y}, (vec_t){my_player->x, my_player->y});
        float dist = v_len(d);

        if(player->hp > my_player->hp) {
            /* silniejszy = zagrożenie */
            if(dist < DANGER_RAD) {
                float score = player->hp/(dist+1);
                if(score > worst_danger_score) {
                    worst_danger_score = score;
                    danger_vec = d; // aktualizuj wektor zagrożenia
                }
            }
        } else if(player->hp < my_player->hp) {
            /* słabszy = potencjalna ofiara */
            if(dist < best_prey_dist) {
                best_prey_dist = dist;
                prey_vec = d; // aktualizuj wektor ofiary
            }
        }
    }

    /* Iskry */
    for(int i = 0; i < MAX_NUMBER_OF_OBJECTS; i++) {
        object_t* spark = &game.sparks[i];
        if(spark->hp <= 0) {
            continue; // pomiń martwe iskry
        }
        vec_t d = v_sub((vec_t){spark->x, spark->y}, (vec_t){my_player->x, my_player->y});
        float dist = v_len(d);
        if(dist < DANGER_RAD) {
            float score = 3.0f / (dist + 1); // iskry są mniej niebezpieczne niż gracze, ale nadal niebezpieczne
            if(score > worst_danger_score) {
                worst_danger_score = score;
                danger_vec = d; // aktualizuj wektor zagrożenia
            }
        }
    }

    /* Jedzenie */
    for(int i = 0 ; i < MAX_NUMBER_OF_OBJECTS; i++) {
        object_t* food = &game.food[i];
        if(food->hp <= 0) {
            continue; // pomiń martwe jedzenie
        }
        vec_t d = v_sub((vec_t){food->x, food->y}, (vec_t){my_player->x, my_player->y});
        float dist = v_len(d);
        if(dist < best_food_dist) {
            best_food_dist = dist;
            food_vec = d; // aktualizuj wektor jedzenia
        }
    }


    const float FOOD_THRESHOLD = 180.0f;
    const float PREY_THRESHOLD = 250.0f;

    vec_t dir;

    if(worst_danger_score > 0) {
        // uciekaj przed zagrożeniem
        dir = v_norm((vec_t){-danger_vec.x, -danger_vec.y});
    } else if(best_food_dist < FOOD_THRESHOLD) {
        // zjedz najblizszy tranzystor, jesli jest blisko
        dir = v_norm(food_vec);
    } else if(best_prey_dist < PREY_THRESHOLD) {
        // brak jedzenia -> poluj na słabszego gracza
        dir = v_norm(prey_vec);
    } else if(best_food_dist < 1e9f) {
        // jedzenie daleko -> idz w jego stronę
        dir = v_norm(food_vec);
    } else {
        // nic ciekawego -> utrzymaj kurs
        dir = (vec_t){cosf(heading), sinf(heading)}; //
    }




    float hw = game.map_width * 0.5f;
    float hh = game.map_height * 0.5f;

    if(my_player->x > hw - SAFE_MARGIN) {
        dir.x -= 0.5f;
    }
    if(my_player->x < -hw + SAFE_MARGIN) {
        dir.x += 0.5f;
    }
    if(my_player->y > hh - SAFE_MARGIN) {
        dir.y -= 0.5f;
    }
    if(my_player->y < -hh + SAFE_MARGIN) {
        dir.y += 0.5f;
    }

    float desired = atan2f(dir.y, dir.x);
    float delta = wrap_signed(desired - heading);
    heading = wrap_signed(heading + delta);

    return wrap_unsigned(heading);
}


void amPacketHandler(const AMCOM_Packet* packet, void* userContext) {
    uint8_t buf[AMCOM_MAX_PACKET_SIZE];              // buffer used to serialize outgoing packets
    size_t toSend = 0;                               // size of the outgoing packet
    SOCKET ConnectSocket  = *((SOCKET*)userContext); // socket used for communication with the server

    switch (packet->header.type) {
    case AMCOM_IDENTIFY_REQUEST:
        printf("Got IDENTIFY.request. Responding with IDENTIFY.response\n");
        AMCOM_IdentifyResponsePayload identifyResponse;
        sprintf(identifyResponse.playerName, "Mortadelka");
        toSend = AMCOM_Serialize(AMCOM_IDENTIFY_RESPONSE, &identifyResponse, sizeof(identifyResponse), buf);
        break;
    case AMCOM_NEW_GAME_REQUEST:
        printf("Got NEW_GAME.request.\n");
        const AMCOM_NewGameRequestPayload* packetNewGame = (const AMCOM_NewGameRequestPayload*)packet->payload;

        game.my_player_number = packetNewGame->playerNumber;
        game.number_of_players = packetNewGame->numberOfPlayers;
        game.map_width = packetNewGame->mapWidth;
        game.map_height = packetNewGame->mapHeight;
        printf("New game with %d players, map size %.2fx%.2f\n", game.number_of_players, game.map_width, game.map_height);


        AMCOM_NewGameResponsePayload newGameResponse;
        sprintf(newGameResponse.helloMessage, "<Powitanie>");
        toSend = AMCOM_Serialize(AMCOM_NEW_GAME_RESPONSE, &newGameResponse, sizeof(newGameResponse), buf);
        break;
    case AMCOM_OBJECT_UPDATE_REQUEST:
        printf("Got OBJECT_UPDATE.request.\n");

        const AMCOM_ObjectUpdateRequestPayload* objectUpdate = (const AMCOM_ObjectUpdateRequestPayload*)packet->payload;

        for(size_t i = 0; i < packet->header.length / sizeof(AMCOM_ObjectState); i++) {
            const AMCOM_ObjectState* object = &objectUpdate->objectState[i];
            switch(object->objectType) {
                case AMCOM_OBJECT_PLAYER:
                    if(object->objectNo < MAX_NUMBER_OF_PLAYERS) {
                        game.players[object->objectNo].hp = object->hp;
                        game.players[object->objectNo].x = object->x;
                        game.players[object->objectNo].y = object->y;
                    } else {
                        printf("Received player object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                case AMCOM_OBJECT_FOOD:
                    if(object->objectNo < MAX_NUMBER_OF_OBJECTS) {
                        game.food[object->objectNo].hp = object->hp;
                        game.food[object->objectNo].x = object->x;
                        game.food[object->objectNo].y = object->y;
                    } else {
                        printf("Received food object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                case AMCOM_OBJECT_SPARK:
                    if(object->objectNo < MAX_NUMBER_OF_OBJECTS) {
                        game.sparks[object->objectNo].hp = object->hp;
                        game.sparks[object->objectNo].x = object->x;
                        game.sparks[object->objectNo].y = object->y;
                    } else {
                        printf("Received spark object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                case AMCOM_OBJECT_GLUE:
                    if(object->objectNo < MAX_NUMBER_OF_OBJECTS) {
                        game.glue[object->objectNo].hp = object->hp;
                        game.glue[object->objectNo].x = object->x;
                        game.glue[object->objectNo].y = object->y;
                    } else {
                        printf("Received glue object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                default:
                    printf("Received object with unknown type: %d\n", object->objectType);
                    break;
            }


        }

        if (game.my_player_number < game.number_of_players) {
            object_t* my_player = &game.players[game.my_player_number];
            printf("My player: HP: %d, Position: (%.2f, %.2f)\n", my_player->hp, my_player->x, my_player->y);
        } else {
            printf("My player number %d is out of range (max %d players)\n", game.my_player_number, game.number_of_players);
        }



        break;
    case AMCOM_MOVE_REQUEST:
        printf("Got MOVE.request.\n");
        AMCOM_MoveResponsePayload moveResponse;

        moveResponse.angle = choose_angle();
        toSend = AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &moveResponse, sizeof(moveResponse), buf);
        break;
    case AMCOM_GAME_OVER_REQUEST:
        printf("Got GAME_OVER.request.\n");
        AMCOM_GameOverResponsePayload gameOverResponse;
        sprintf(gameOverResponse.endMessage, "<Pozegnanie>");
        toSend = AMCOM_Serialize(AMCOM_GAME_OVER_RESPONSE, &gameOverResponse, sizeof(gameOverResponse), buf);
        break;
    default:
        printf("Got unknown packet type: %d\n", packet->header.type);
        break;
    }

	// if there is something to send back - do it
	if (toSend > 0) {
		int bytesSent = send(ConnectSocket, (const char*)buf, toSend, 0 );
		if (bytesSent == SOCKET_ERROR) {
			printf("Socket send failed with error: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			return;
		}
	}
}


#define GAME_SERVER "localhost"
#define GAME_SERVER_PORT "2001"

int main(int argc, char **argv) {
    printf("This is mniAM player. Let's eat some transistors! \n");

    WSADATA wsaData;
    int iResult;

    // Initialize Winsock library (windows sockets)
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    // Prepare temporary data
    SOCKET ConnectSocket  = INVALID_SOCKET;
    struct addrinfo *result = NULL;
    struct addrinfo *ptr = NULL;
    struct addrinfo hints;
    int iSendResult;
    char recvbuf[512];
    int recvbuflen = sizeof(recvbuf);

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the game server address and port
    iResult = getaddrinfo(GAME_SERVER, GAME_SERVER_PORT, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    printf("Connecting to game server...\n");
    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
                ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }
    // Free some used resources
    freeaddrinfo(result);

    // Check if we connected to the game server
    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to the game server!\n");
        WSACleanup();
        return 1;
    } else {
        printf("Connected to game server\n");
    }

    AMCOM_Receiver amReceiver;
    AMCOM_InitReceiver(&amReceiver, amPacketHandler, &ConnectSocket);

    // Receive until the peer closes the connection
    do {

        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if ( iResult > 0 ) {
            AMCOM_Deserialize(&amReceiver, recvbuf, iResult);
        } else if ( iResult == 0 ) {
            printf("Connection closed\n");
        } else {
            printf("recv failed with error: %d\n", WSAGetLastError());
        }

    } while( iResult > 0 );

    // No longer need the socket
    closesocket(ConnectSocket);
    // Clean up
    WSACleanup();

    return 0;
}
