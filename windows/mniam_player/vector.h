/**
* @author Jakub Bubak
* @date 10.06.2025
*/

#pragma once
#include <math.h>


/**
 * @struct vec_t
 * @brief A 2D vector structure.
 */
typedef struct {
		float x;
		float y;
} vec_t;

/**
 * @brief Initializes a 2D vector with given x and y coordinates.
 * @param x The x coordinate.
 * @param y The y coordinate.
 * @return A vec_t structure initialized with the given coordinates.
 */
vec_t v_init(float x, float y);

/**
 * @brief Subtracts two vectors.
 * @param a The first vector.
 * @param b The second vector.
 * @return A new vec_t structure representing the result of the subtraction (a - b).
 */
vec_t v_sub(vec_t a, vec_t b);

/** * @brief Calculates the length of a vector.
 * @param v The vector.
 * @return The length of the vector.
 */
float v_len(vec_t v);

/**
 * @brief Normalizes a vector (scales it to unit length).
 * @param v The vector to normalize.
 * @return A new vec_t structure representing the normalized vector.
 */
vec_t v_norm(vec_t v);

/**
 * @brief Calculates the angle of a vector in radians.
 * @param v The vector.
 * @return The angle of the vector in radians, in the range [0, 2π).
 */
float v_angle(vec_t v);

/**
 * @brief Calculates the dot product of two vectors.
 * @param a The first vector.
 * @param b The second vector.
 * @return The dot product of the two vectors, while ensuring both are normalized.
 */
float v_dot(vec_t a, vec_t b);


float v_cross(vec_t a, vec_t b);