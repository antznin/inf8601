/*
 i* dragon_tbb.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#include <iostream>

extern "C" {
#include "dragon.h"
#include "color.h"
#include "utils.h"
}
#include "dragon_tbb.h"
#include "tbb/tbb.h"
#include "TidMap.h"

using namespace std;
using namespace tbb;

class DragonLimits {
    public:
	piece_t pieces[NB_TILES];
    
    	void operator()(const blocked_range<size_t>& r ){
	    int start = r.begin();
	    int end = r.end();
	    for (int i = 0; i < NB_TILES; i++) {
		    piece_limit(start, end, &pieces[i]);
	    }
    	}
    
    	void join(const DragonLimits& other){
	    for (int i = 0; i < NB_TILES; i++) {
		    piece_merge(&pieces[i],other.pieces[i], tiles_orientation[i]);	
	    }
    	}
    
    	DragonLimits( DragonLimits& other, split){
	    for (int i = 0; i < NB_TILES; i++) {
		piece_init(& pieces[i]);
		pieces[i].orientation = tiles_orientation[i];   
	    }     
   	}
    DragonLimits(){
	for (int i = 0; i < NB_TILES; i++) {
		    piece_init(& pieces[i]);
		    pieces[i].orientation = tiles_orientation[i];   
	}     
    }

};


class DragonClear
{
  public:
	draw_data *data;
	DragonClear(const DragonClear &d) : data(d.data){}
	DragonClear(draw_data *other_data): data(other_data){}
	void operator()(const blocked_range<size_t> &r) const
	{
		init_canvas(r.begin(), r.end(), data->dragon, -1);
	}
};

class DragonDraw
{
  public:
	draw_data *data;
	DragonDraw(draw_data *other_data) : data(other_data)
	{}

	void operator()(const blocked_range<uint64_t> &r) const
	{
		xy_t position;
		xy_t orientation;
		uint64_t n;
		for (size_t k = 0; k < NB_TILES; k++)
		{
			//dragon_draw_raw(k, r.begin(), r.end(), this->data->dragon, this->data->dragon_width, this->data->dragon_height, this->data->limits, _tidMap->getIdFromTid(gettid()), 1);
			
            		position = compute_position(k, r.begin(), 1);
			orientation = compute_orientation(k, r.begin(), 1);
			position.x -= data->limits.minimums.x;
			position.y -= data->limits.minimums.y;

			for (n = r.begin() + 1; n <= r.end(); n++)
			{
				int j = (position.x + (position.x + orientation.x)) >> 1;
				int i = (position.y + (position.y + orientation.y)) >> 1;
				int index = i * data->dragon_width + j;

				data->dragon[index] = n * data->nb_thread / data->size;

				position.x += orientation.x;
				position.y += orientation.y;
				if (((n & -n) << 1) & n)
					rotate_left(&orientation);
				else
					rotate_right(&orientation);
			}//*/
		}
	}
};

class DragonRender
{
  public:
	DragonRender(const DragonRender &d) : data(d.data){}
	DragonRender(draw_data *other_data) : data(other_data){}

	void operator()(const blocked_range<uint64_t> &r) const
	{
		scale_dragon(r.begin(),
			     r.end(),
			     data->image,
			     data->image_width,
			     data->image_height,
			     data->dragon,
			     data->dragon_width,
			     data->dragon_height,
			     data->palette);
	}

  private:
	draw_data *data;
};


int dragon_draw_tbb(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	struct draw_data data;
	limits_t limits;
	char *dragon = NULL;
	int dragon_width;
	int dragon_height;
	int dragon_surface;
	int scale_x;
	int scale_y;
	int scale;
	int deltaJ;
	int deltaI;
	struct palette *palette = init_palette(nb_thread);
	if (palette == NULL)
		return -1;

	/* 1. Calculer les limites du dragon */
	dragon_limits_tbb(&limits, size, nb_thread);
	task_scheduler_init init(nb_thread);

	dragon_width = limits.maximums.x - limits.minimums.x;
	dragon_height = limits.maximums.y - limits.minimums.y;
	dragon_surface = dragon_width * dragon_height;
	scale_x = dragon_width / width + 1;
	scale_y = dragon_height / height + 1;
	scale = (scale_x > scale_y ? scale_x : scale_y);
	deltaJ = (scale * width - dragon_width) / 2;
	deltaI = (scale * height - dragon_height) / 2;

	dragon = (char *)malloc(dragon_surface);
	if (dragon == NULL)
	{
		free_palette(palette);
		return -1;
	}

	data.nb_thread = nb_thread;
	data.dragon = dragon;
	data.image = image;
	data.size = size;
	data.image_height = height;
	data.image_width = width;
	data.dragon_width = dragon_width;
	data.dragon_height = dragon_height;
	data.limits = limits;
	data.scale = scale;
	data.deltaI = deltaI;
	data.deltaJ = deltaJ;
	data.palette = palette;
	data.tid = (int *)calloc(nb_thread, sizeof(int));

	/* 2. Initialiser la surface : DragonClear */
	DragonClear dragon_clear(&data);
	parallel_for(blocked_range<size_t>(0, dragon_surface), dragon_clear);

	/* 3. Dessiner le dragon : DragonDraw */


	DragonDraw dragon_draw(&data);
	parallel_for(blocked_range<size_t>(0, data.size), dragon_draw);

	/* 4. Effectuer le rendu final */
	DragonRender dragon_render(&data);
	parallel_for(blocked_range<size_t>(0, data.image_height), dragon_render);

	free_palette(palette);
	FREE(data.tid);
	
	*canvas = dragon;
	return 0;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_tbb(limits_t *limits, uint64_t size, int nb_thread)
{
	DragonLimits lim;

    
    tbb::task_scheduler_init init(nb_thread);

	/* 1. Calculer les limites */
    parallel_reduce(blocked_range<size_t>(0,size), lim);
	/* La limite globale est calculée à partir des limites
	 * de chaque dragon.
	 */
	merge_limits(&lim.pieces[0].limits, &lim.pieces[1].limits);
	merge_limits(&lim.pieces[0].limits, &lim.pieces[2].limits);
	merge_limits(&lim.pieces[0].limits, &lim.pieces[3].limits);

	*limits = lim.pieces[0].limits;
	return 0;
}
