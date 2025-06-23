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
#include <stdbool.h>



/* --- Configuration constants --- */
#define MAX_NUMBER_OF_PLAYERS 8

#define MAX_NUMBER_OF_FOOD 100

#define MAX_NUMBER_OF_SPARKS 8
#define MAX_NUMBER_OF_GLUE 8

/* --- Type declarations --- */

/**
 * @brief Enumeration of all object types communicated through AMCOM.
 */
enum AMCOM_ObjectType {
    AMCOM_OBJECT_PLAYER = 0,
    AMCOM_OBJECT_FOOD = 1,
    AMCOM_OBJECT_SPARK = 2,
    AMCOM_OBJECT_GLUE = 3
};

/**
 * @brief Generic object present on the map
 */
typedef struct {
    int8_t hp;
    float x;
    float y;
} object_t;

/**
 * @brief Wrapper storing a candidate target with vector and score
 */
typedef struct {
    const object_t* obj;
    float score;
    vec_t vector;
} target_t;

/**
 * @brief Global game state reconstructed from AMCOM updates.
 *
 * The client copy of game state, used to make decisions about the next move.
 */
typedef struct {
    uint8_t my_player_number;

    uint8_t number_of_players;
    object_t players[MAX_NUMBER_OF_PLAYERS];

    object_t food[MAX_NUMBER_OF_FOOD];
    uint8_t number_of_food;

    object_t sparks[MAX_NUMBER_OF_SPARKS];
    uint8_t number_of_sparks;

    object_t glue[MAX_NUMBER_OF_GLUE];
    uint8_t number_of_glue;

    float map_width;
    float map_height;
} game_t;

/* --- Global variables --- */

/* * @brief Latest game state
 */
game_t game = {
    .my_player_number = 0,
    .number_of_players = 0,
    .map_width = 0.0f,
    .map_height = 0.0f,
    .number_of_food = 0,
    .number_of_sparks = 0,
    .number_of_glue = 0
};

/* * @brief Pointer to the player object of the current player
 */
object_t* my_player = NULL;

/**
 * @brief Calculate the danger radius based on player and target HP.
 *
 * The danger radius is defined as twice the sum of the player and target radius.
 * The player and target basic diameter is assumed to be 25, so the formula is:
 * danger_radius = 2 * (25 + player_hp + 25 + target_hp)
 *
 * @param player_hp HP of the player
 * @param target_hp HP of the target
 * @return Calculated danger radius
 */
float calculate_danger_radius(float player_hp, float target_hp) {
    return 2*(25 + player_hp + 25 + target_hp);
}

/**
 * @brief Calculate a score based on HP and distance.
 *
 * The score is calculated as:
 * score = hp / (distance / (map_height * sqrt(2)))
 *
 * This normalizes the distance based on the map height and gives a higher score
 * for closer objects with higher HP.
 *
 * @param hp HP of the object
 * @param distance Distance to the object
 * @return Calculated score
 */
float calculate_score(float hp, float distance) {
    float normalized_distance = distance/(game.map_height * sqrt(2));

    return hp/normalized_distance;
}



/**
 * @brief Check if there is glue on the path to the given vector.
 *
 * This function checks if any glue object is within a certain angle range
 * of the vector from the player to the glue. If glue is found, it returns true.
 *
 * @param vector The vector to check against glue positions
 * @return true if glue is on the path, false otherwise
 */
bool is_glue_on_path(vec_t vector) {
    for(uint8_t i = 0; i < game.number_of_glue; i++) {
        const object_t* glue = &game.glue[i];

        //skip dead glue
        if(glue->hp <= 0) {
            continue;
        }

        vec_t glue_vector = v_init(glue->x - my_player->x, glue->y - my_player->y);
        float glue_distance = v_len(glue_vector) - 100; //100 is the glue radius
        float distance = v_len(vector);

        if(glue_distance < distance) {
            float glue_angle = atan2f(100, glue_distance);

            if(v_angle(vector) < v_angle(glue_vector) + glue_angle && v_angle(vector) > v_angle(glue_vector) - glue_angle) {
                return true;
            }
        }
    }
    return false;
}




/**
 * @brief Choose the angle for the next move based on the game state.
 *
 * This function analyzes the game state and decides the best angle to move
 * based on the current player's HP, nearby players, food, sparks, and glue.
 * It returns an angle in radians.
 *
 * @return The chosen angle in radians
 */
float choose_angle(void) {
    // If last player, circle around center
    bool am_last = true;
    for (int i = 0; i < game.number_of_players; i++) {
        if (i != game.my_player_number && game.players[i].hp > 0) {
            am_last = false;
            break;
        }
    }

    if(am_last) {
        const float circle_radius = 200;

        static float phi = 0.0f;
        const float DELTA = M_PI/ 36.0f; // 5 degrees step

        phi += DELTA;
        while(phi < 0.0f) phi += 2.0f * M_PI;
        while(phi >= 2.0f * M_PI) phi -= 2.0f * M_PI;

        vec_t circle_point = v_init(circle_radius * cosf(phi), circle_radius * sinf(phi));
        vec_t my_position = v_init(my_player->x, my_player->y);
        vec_t vector_to_circle = v_sub(circle_point, my_position);

        return v_angle(vector_to_circle);
    }


    //current angle of movement (in radians)
    static float direction = 0.0f;

    target_t worst_danger = {
        .obj = NULL,
        .score = 0.0f,
        .vector = v_init(0, 0)
    };

    target_t worst_spark = {
        .obj = NULL,
        .score = 0.0f,
        .vector = v_init(0, 0)
    };

    target_t best_food = {
        .obj = NULL,
        .score = 0.0f,
        .vector = v_init(0, 0)
    };

    target_t best_prey = {
        .obj = NULL,
        .score = 0.0f,
        .vector = v_init(0, 0)
    };

    /* PLAYERS */
    for(uint8_t i = 0; i < game.number_of_players; i++) {
        //skip myself
        if(i == game.my_player_number) {
            continue;
        }

        object_t* player = &game.players[i];
        if(player->hp <= 0) {
            continue; // skip dead players
        }

        vec_t player_vector = v_init(player->x - my_player->x, player->y - my_player->y);
        float player_distance = v_len(player_vector);

        // if player is stronger, consider it as danger
        if(player->hp > my_player->hp) {
            if(player_distance < calculate_danger_radius(my_player->hp, player->hp)) {
                float score = calculate_score(1, player_distance);
                if(score > worst_danger.score) {
                    worst_danger.score = score;
                    worst_danger.obj = player;
                    worst_danger.vector = player_vector;
                }
            }
        } else if(player->hp < my_player->hp) { // if player is weaker, consider it as prey
            float score = calculate_score(player->hp, player_distance);
            if(score > best_prey.score) {
                best_prey.score = score;
                best_prey.obj = player;
                best_prey.vector = player_vector;
            }
        }
    }

    /* SPARKS */
    for(uint8_t i = 0; i < game.number_of_sparks; i++) {
        object_t* spark = &game.sparks[i];
        if(spark->hp <= 0) {
            continue; // skip dead sparks
        }

        vec_t spark_vector = v_init(spark->x - my_player->x, spark->y - my_player->y);
        float spark_distance = v_len(spark_vector);

        if(spark_distance < (25+my_player->hp+25+50)) { // danger radius for sparks
            float score = calculate_score(1, spark_distance);
            if(score > worst_spark.score) {
                worst_spark.score = score;
                worst_spark.obj = spark;
                worst_spark.vector = spark_vector;
            }
        }
    }


    /* FOOD */
    for(uint8_t i = 0; i < game.number_of_food; i++) {
        object_t* food = &game.food[i];

        if(food->hp <= 0) {
            continue; // skip dead food
        }

        vec_t food_vector = v_init(food->x - my_player->x, food->y - my_player->y);
        float food_distance = is_glue_on_path(food_vector) ? v_len(food_vector) * 20 : v_len(food_vector); // if glue is on the path, increase distance
        float food_score = 2*calculate_score(food->hp, food_distance);

        if(food_score > best_food.score) {
            best_food.score = food_score;
            best_food.obj = food;
            best_food.vector = food_vector;
        }
    }

    // Debugging output to see the scores of different targets
    // printf("Scores: Danger: %.2f, Spark: %.2f, Food: %.2f, Prey: %.2f\n", worst_danger.score, worst_spark.score, best_food.score, best_prey.score);


    if(worst_danger.score > 0 || worst_spark.score > 0) {
        if(worst_danger.score > worst_spark.score) {
            // if danger is worse than spark, run away from danger
            if(v_len(worst_danger.vector) < (25 + my_player->hp + 25 + 100)) {
                // if danger is too close, run away in the opposite direction
                direction = v_angle(worst_danger.vector) + M_PI;
            } else {
                // if danger is not that close, run away in a perpendicular direction
                direction = v_angle(worst_danger.vector) + M_PI_2;
            }
            // Debugging output to see the direction we are running away from danger
            // printf("Running away from danger: %f\n", direction * (180.0f / M_PI));
            return direction;
        }
        // if spark is worse than danger, run away from spark
        direction = v_angle(worst_spark.vector) + M_PI_2;
        // Debugging output to see the direction we are running away from spark
        // printf("Running away from spark: %f\n", direction * (180.0f / M_PI));
        return direction;
    }
    // if there is no danger or spark, go for food or prey
    if(best_food.score > best_prey.score && best_food.score > 0) {
        // if there is food, go for it
        direction = v_angle(best_food.vector);
        // Debugging output to see the direction we are going for food
        // printf("Going for food: %f\n", direction * (180.0f / M_PI));
        return direction;
    }

    if(best_prey.score > 0) {
        // if there is prey, go for it
        direction = v_angle(best_prey.vector);
        // Debugging output to see the direction we are going for prey
        // printf("Going for prey: %f\n", direction * (180.0f / M_PI));
        return direction;
    }
    // if there is no food or prey, just go in the current direction
    return direction;
}

void amPacketHandler(const AMCOM_Packet* packet, void* userContext) {
    uint8_t buf[AMCOM_MAX_PACKET_SIZE];              // buffer used to serialize outgoing packets
    size_t toSend = 0;                               // size of the outgoing packet
    SOCKET ConnectSocket  = *((SOCKET*)userContext); // socket used for communication with the server

    switch (packet->header.type) {
    case AMCOM_IDENTIFY_REQUEST:
        AMCOM_IdentifyResponsePayload identifyResponse;
        sprintf(identifyResponse.playerName, "PudziAM");
        toSend = AMCOM_Serialize(AMCOM_IDENTIFY_RESPONSE, &identifyResponse, sizeof(identifyResponse), buf);
        break;
    case AMCOM_NEW_GAME_REQUEST:
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
                    if(object->objectNo < MAX_NUMBER_OF_FOOD) {
                        game.food[object->objectNo].hp = object->hp;
                        game.food[object->objectNo].x = object->x;
                        game.food[object->objectNo].y = object->y;
                        game.number_of_food++;
                    } else {
                        printf("Received food object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                case AMCOM_OBJECT_SPARK:
                    if(object->objectNo < MAX_NUMBER_OF_SPARKS) {
                        game.sparks[object->objectNo].hp = object->hp;
                        game.sparks[object->objectNo].x = object->x;
                        game.sparks[object->objectNo].y = object->y;
                        game.number_of_sparks++;
                    } else {
                        printf("Received spark object with invalid number: %d\n", object->objectNo);
                    }
                    break;
                case AMCOM_OBJECT_GLUE:
                    if(object->objectNo < MAX_NUMBER_OF_GLUE) {
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
            my_player = &game.players[game.my_player_number];
            // printf("My player: HP: %d, Position: (%.2f, %.2f)\n", my_player->hp, my_player->x, my_player->y);
        } else {
            printf("My player number %d is out of range (max %d players)\n", game.my_player_number, game.number_of_players);
        }



        break;
    case AMCOM_MOVE_REQUEST:
        AMCOM_MoveResponsePayload moveResponse;

        moveResponse.angle = choose_angle();
        toSend = AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &moveResponse, sizeof(moveResponse), buf);
        break;
    case AMCOM_GAME_OVER_REQUEST:
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
