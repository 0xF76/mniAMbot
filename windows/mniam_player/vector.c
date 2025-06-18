/**
* @author Jakub Bubak
* @date 10.06.2025
*/

#include "vector.h"

vec_t v_init(float x, float y) {
	vec_t result;
	result.x = x;
	result.y = y;
	return result;
}

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
		result.x = v.x / len;
		result.y = v.y / len;
	} else {
		// If the length is too small, return a zero vector
		result.x = 0.0f;
		result.y = 0.0f;
	}
	return result;
}


float v_angle(vec_t v) {
	float angle = atan2f(v.y, v.x);

	//ensure angle is in the range [0, 2π)
	while(angle < 0.0f) {
        angle += 2.0f * M_PI;
    }

	return angle;
}


float v_dot(vec_t a, vec_t b) {
	a = v_norm(a);
	b = v_norm(b);
	return a.x * b.x + a.y * b.y;
}

float v_cross(vec_t a, vec_t b) {
	a = v_norm(a);
	b = v_norm(b);
	return a.x * b.y - a.y * b.x;
}
