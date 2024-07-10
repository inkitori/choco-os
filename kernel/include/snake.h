#ifndef SNAKE_H
#define SNAKE_H

#include "stdint.h"

#define GRID_SIZE 20

typedef enum Direction
{
	UP,
	DOWN,
	LEFT,
	RIGHT
} Direction;

typedef struct SnakeBody
{
	uint8_t coordinates_x;
	uint8_t coordinates_y;

} SnakeBody;

void snake_init();

#endif