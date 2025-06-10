/**
* @author Jakub Bubak
* @date 10.06.2025
*/

#pragma once
#include <math.h>

typedef struct {
		float x;
		float y;
} vec_t;


vec_t v_init(float x, float y);
vec_t v_sub(vec_t a, vec_t b);
float v_len(vec_t v);
vec_t v_norm(vec_t v);
float v_angle(vec_t v);