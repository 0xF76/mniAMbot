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

#include "vector.h"

#define MAX_NUMBER_OF_PLAYERS 8
#define MAX_NUMBER_OF_OBJECTS 100


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
    uint8_t number_of_food;

    object_t sparks[MAX_NUMBER_OF_OBJECTS];
    uint8_t number_of_sparks;

    object_t glue[MAX_NUMBER_OF_OBJECTS];
    uint8_t number_of_glue;

    float map_width;
    float map_height;
} game_t;

game_t game = {
    .my_player_number = 0,
    .number_of_players = 0,
    .map_width = 0.0f,
    .map_height = 0.0f
};

float choose_angle(void) {
    static float direction = 0.0f; // angle in radians

    const float DANGER_THRESHOLD = 100.0f;
    const float FOOD_THRESHOLD = 500.0f;

    const object_t* worst_danger = NULL;
    float worst_danger_score = 0.0f;
    vec_t worst_danger_vec = v_init(0.0f, 0.0f);

    float best_food_score = 0.0f;
    vec_t best_food_vec = v_init(0.0f, 0.0f);

    float best_prey_score = 0.0f;
    vec_t best_prey_vec = v_init(0.0f, 0.0f);


    object_t* my_player = &game.players[game.my_player_number];

    /* PLAYERS */
    for(uint8_t i = 0; i < game.number_of_players; i++) {
        // skip yourself
        if(i == game.my_player_number) {
            continue;
        }

        const object_t* player = &game.players[i];
        //skip dead players
        if(player->hp <= 0) {
            continue;
        }


        vec_t v = v_init(player->x - my_player->x, player->y - my_player->y);
        float distance = v_len(v);



        if(player->hp + 3 > my_player->hp) {
            if(distance < DANGER_THRESHOLD) {
                float score = player->hp/(distance+1);
                if(score > worst_danger_score) {
                    worst_danger = player;
                    worst_danger_score = score;
                    worst_danger_vec = v;
                }
            }
        } else {
            float score = player->hp / (distance + 1.0f);
            if(score > best_prey_score) {
                best_prey_score = score;
                best_prey_vec = v;
            }
        }
    }

    /* SPARKS */
    for(uint8_t i = 0; i < game.number_of_sparks; i++) {
        const object_t* spark = &game.sparks[i];

        //skip dead sparks
        if(spark->hp <= 0) {
            continue;
        }

        vec_t v = v_init(spark->x - my_player->x, spark->y - my_player->y);
        float distance = v_len(v);

        if(distance < DANGER_THRESHOLD) {
            float score = 1.0f/ (distance + 1.0f);
            if(score > worst_danger_score) {
                worst_danger = spark;
                worst_danger_score = score;
                worst_danger_vec = v;
            }
        }
    }


    /* FOOD */
    for(uint8_t i = 0; i < game.number_of_food; i++) {
        const object_t* food = &game.food[i];

        //skip dead food
        if(food->hp <= 0) {
            continue;
        }

        vec_t v = v_init(food->x - my_player->x, food->y - my_player->y);
        float distance = v_len(v);


        float danger_angle = M_PI/4;
        if(worst_danger != NULL) {
            danger_angle += atan2f((25.0f + (float)worst_danger->hp)/2, distance);
        }



        //skip food that is too close to a danger
        if(v_angle(v) < v_angle(worst_danger_vec) + danger_angle && v_angle(v) > v_angle(worst_danger_vec) - danger_angle && distance > v_len(worst_danger_vec)) {
            continue;
        }

        /* GLUE */
        for(uint8_t i = 0; i < game.number_of_glue; i++) {
            const object_t* glue = &game.glue[i];
            //skip dead glue
            if(glue->hp <= 0) {
                continue;
            }
            vec_t glue_vec = v_init(glue->x - my_player->x, glue->y - my_player->y);
            float glue_distance = v_len(glue_vec) - 100.0f; // 100 is the glue radius

            if(glue_distance < distance) {
                float glue_angle = atan2f(100, glue_distance);

                if(v_angle(v) < v_angle(glue_vec) + glue_angle && v_angle(v) > v_angle(glue_vec) - glue_angle) {
                    distance *= 20; // if the food is in the glue radius, make it harder to reach
                }
            }
        }

        float score = (float)food->hp / (distance + 1.0f);
        if(distance < FOOD_THRESHOLD && score > best_food_score) {
            best_food_score = score;
            best_food_vec = v;
        }
    }

    if(best_food_score > 0) {
        direction = v_angle(best_food_vec);
    } else if(best_prey_score >0) {
        direction = v_angle(best_prey_vec);
    } else {
        direction = v_angle(worst_danger_vec) + (float)M_PI_2;
    }

    return direction;
}


void amPacketHandler(const AMCOM_Packet* packet, void* userContext) {
    uint8_t buf[AMCOM_MAX_PACKET_SIZE];              // buffer used to serialize outgoing packets
    size_t toSend = 0;                               // size of the outgoing packet
    SOCKET ConnectSocket  = *((SOCKET*)userContext); // socket used for communication with the server

    switch (packet->header.type) {
    case AMCOM_IDENTIFY_REQUEST:
        printf("Got IDENTIFY.request. Responding with IDENTIFY.response\n");
        AMCOM_IdentifyResponsePayload identifyResponse;
        sprintf(identifyResponse.playerName, "PudziAM");
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
        sprintf(newGameResponse.helloMessage, "Dawaj na ring!");
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
                        game.number_of_food++;
                    } else {
                        printf("Received food object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                case AMCOM_OBJECT_SPARK:
                    if(object->objectNo < MAX_NUMBER_OF_OBJECTS) {
                        game.sparks[object->objectNo].hp = object->hp;
                        game.sparks[object->objectNo].x = object->x;
                        game.sparks[object->objectNo].y = object->y;
                        game.number_of_sparks++;
                    } else {
                        printf("Received spark object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                case AMCOM_OBJECT_GLUE:
                    if(object->objectNo < MAX_NUMBER_OF_OBJECTS) {
                        game.glue[object->objectNo].hp = object->hp;
                        game.glue[object->objectNo].x = object->x;
                        game.glue[object->objectNo].y = object->y;
                        game.number_of_glue++;
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
        sprintf(gameOverResponse.endMessage, "To by nic nie dalo...");
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
