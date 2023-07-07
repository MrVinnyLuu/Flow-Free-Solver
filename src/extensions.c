#include "extensions.h"
#include "options.h"

///////////////////////////////////////////////////////
// x and y coordinate deltas to check the 12-cell cross
// around a cell
const int TWELVE_CELL_CROSS_DELTA[12][2] = {
	{ 0, -1},
	{ 1,  0},
	{ 0,  1},
	{-1,  0},
	{-1, -1},
	{ 1, -1},
	{ 1,  1},
	{-1,  1},
	{ 0, -2},
	{ 2,  0},
	{ 0,  2},
	{-2,  0}
};

//////////////////////////////////////////////////////////////////////
// For sorting colors

int color_features_compare(const void* vptr_a, const void* vptr_b) {

	const color_features_t* a = (const color_features_t*)vptr_a;
	const color_features_t* b = (const color_features_t*)vptr_b;

	int u = cmp(a->user_index, b->user_index);
	if (u) { return u; }

	int w = cmp(a->wall_dist[0], b->wall_dist[0]);
	if (w) { return w; }

	int g = -cmp(a->wall_dist[1], b->wall_dist[1]);
	if (g) { return g; }

	return -cmp(a->min_dist, b->min_dist);

}

//////////////////////////////////////////////////////////////////////
// Place the game colors into a set order

void game_order_colors(game_info_t* info,
                       game_state_t* state) {

	if (g_options.order_random) {
    
		srand(now() * 1e6);
    
		for (size_t i=info->num_colors-1; i>0; --i) {
			size_t j = rand() % (i+1);
			int tmp = info->color_order[i];
			info->color_order[i] = info->color_order[j];
			info->color_order[j] = tmp;
		}

	} else { // not random

		color_features_t cf[MAX_COLORS];
		memset(cf, 0, sizeof(cf));

		for (size_t color=0; color<info->num_colors; ++color) {
			cf[color].index = color;
			cf[color].user_index = MAX_COLORS;
		}
    

		for (size_t color=0; color<info->num_colors; ++color) {
			
			int x[2], y[2];
			
			for (int i=0; i<2; ++i) {
				pos_get_coords(state->pos[color], x+i, y+i);
				cf[color].wall_dist[i] = get_wall_dist(info, x[i], y[i]);
			}

			int dx = abs(x[1]-x[0]);
			int dy = abs(y[1]-y[0]);
			
			cf[color].min_dist = dx + dy;
			
		

		}


		qsort(cf, info->num_colors, sizeof(color_features_t),
		      color_features_compare);

		for (size_t i=0; i<info->num_colors; ++i) {
			info->color_order[i] = cf[i].index;
		}
    
	}

	if (!g_options.display_quiet) {

		printf("\n************************************************"
		       "\n*               Branching Order                *\n");
		if (g_options.order_most_constrained) {
			printf("* Will choose color by most constrained\n");
		} else {
			printf("* Will choose colors in order: ");
			for (size_t i=0; i<info->num_colors; ++i) {
				int color = info->color_order[i];
				printf("%s", color_name_str(info, color));
			}
			printf("\n");
		}
		printf ("*************************************************\n\n");

	}

}

//////////////////////////////////////////////////////////////////////
// Check for dead-end regions of freespace where there is no way to
// put an active path into and out of it. Any freespace node which
// has only one free neighbor represents such a dead end. For the
// purposes of this check, cur and goal positions count as "free".

int game_check_deadends(const game_info_t* info,
                        const game_state_t* state) {
	
	// Get the coordinates of the last moves
	int last_move_x, last_move_y;
	pos_get_coords(state->pos[state->last_color], &last_move_x, &last_move_y);

	// Check the whether any cell in the 12-cell cross around 
	// the last move is a dead-end cell
	for (int i = 0; i <= 12; i++) {

		if (game_is_dead_cell(info, state,
			last_move_x + TWELVE_CELL_CROSS_DELTA[i][0],
			last_move_y + TWELVE_CELL_CROSS_DELTA[i][1])) {

			return 1;

		}
		
	}
	
	// If code reaches this point, the last move doesn't create a dead-end
	return 0;

}

//////////////////////////////////////////////////////////////////////
// Check if the cell (x,y) is a dead-end cell

int game_is_dead_cell(const game_info_t* info,
                         const game_state_t* state,
                         int x, int y) {
	
    // Walls can't be dead-end cells
	if (!coords_valid(info, x, y)) return 0;
	
	// Get information about the cell
	pos_t cell_pos = pos_from_coords(x, y);
    cell_t cell_type = state->cells[cell_pos];

	// Dead-end detection has only been implemented for free cells, goal cells
	// and the current position of in-progress paths
	if (!(cell_type == TYPE_FREE || cell_type == TYPE_GOAL 
		|| cell_pos == state->pos[cell_get_color(state->cells[cell_pos])])) {
		return 0;
	}

	int num_free = 0;

	// Loop through the 4 neighbour cells of cell (x,y)
	for (int dir = 0; dir < 4; dir++) {

		// Get information about the neighbour cell
		pos_t neighbor_pos = offset_pos(info, x, y, dir);
		int neighbor_color = cell_get_color(state->cells[neighbor_pos]);
		cell_t neighbor_type = cell_get_type(state->cells[neighbor_pos]);

		/*
		The conditions for a dead-end cell are different depending on whether
		said cell is free, a goal cell or the current position of an 
		in-progress pipe:

		For a neighbour of a free cell to be considered free, the following
		conditions must all be met:
		- Condition 1: The neighbour cell isn't a wall.
		- Condition 2: The neighbour cell isn't part of a completed path,
						i.e. if the neighbour isn't empty, then the 
						neighbour's color is not marked as completed.	
		- Condition 3: The neighbour cell is an empty cell, an uncompelted
						goal or the current position of an in-progress path.
		*/
		if (cell_type == TYPE_FREE
			// Condition 1:
			&& neighbor_pos != INVALID_POS 
			// Condition 2:
			&& !(neighbor_type != TYPE_FREE && state->completed & (1 << neighbor_color))
			// Condition 3:
			&& (neighbor_type == TYPE_FREE
				|| neighbor_type == TYPE_GOAL
				|| neighbor_pos == state->pos[neighbor_color])) {

			num_free++;

			// If there are at least 2 free cells around a free cell, it isn't
			// a dead-end cell so we don't need to keep checking
			if (num_free >= 2) return 0;


		/* 
		For a neighbour of a goal cell to be considered free, the following
		conditions must all be met:
		- Condition 1: The neighbour cell isn't a wall.
		- Condition 2: The neighbour cell is an empty cell or the current
					   position of an in-progress path.
		*/
		} else if (cell_type == TYPE_GOAL
				   // Condition 1:
				   && neighbor_pos != INVALID_POS
				   // Condition 2:
				   && (neighbor_type == TYPE_FREE
				       || neighbor_pos == state->pos[neighbor_color])) {
			
			// If there are any free cells around a goal cell, it isn't
			// a dead-end cell so we don't need to keep checking
			return 0;


		/* 
		For a neighbour of a current position of an in-progress path to be
		considered free, the following conditions must all be met:
		- Condition 1: The neighbour cell isn't a wall.
		- Condition 2: The neighbour cell is an empty cell or is an
					   uncompelted goal.
		*/
		} else if (cell_pos == state->pos[cell_get_color(state->cells[cell_pos])]
				   // Condition 1:
				   && neighbor_pos != INVALID_POS
				   // Condition 2:
				   && (neighbor_type == TYPE_FREE
					   || (neighbor_type == TYPE_GOAL 
						   && state->completed & (1 << neighbor_color)))) {
			
			// If there are any free cells around a a current position of an 
			// in-progress path, it isn't a dead-end cell
			return 0;

		}

	}

	// If code reaches this point, the cell has to be a dead-end cell
	return 1;

}                    
