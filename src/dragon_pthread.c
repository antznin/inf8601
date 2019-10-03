/*
 * dragon_pthread.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "dragon.h"
#include "color.h"
#include "dragon_pthread.h"

#define PRINT_PTHREAD_ERROR(err, msg) \
	do { errno = err; perror(msg); } while(0)

pthread_mutex_t mutex_stdout;

void printf_threadsafe(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	pthread_mutex_lock(&mutex_stdout);
	vprintf(format, ap);
	pthread_mutex_unlock(&mutex_stdout);
	va_end(ap);
}

/**
 * Does the work on a part of the dragon.
 * Returns 0 on success.
 */
void* dragon_draw_worker(void *data)
{


	/* 1. Initialiser la surface */

	struct draw_data * drawing_data = (struct draw_data *)data;

	int id = drawing_data->id;

	int height       = drawing_data->dragon_height;
	int width        = drawing_data->dragon_width;
	uint64_t surface = height * width;
	int canvas_begin = surface * id / drawing_data->nb_thread;
	int canvas_end   = surface * (id + 1) / drawing_data->nb_thread;
	/* int canvas_end   = (id == drawing_data->nb_thread - 1) ? */ 
	/* 	surface : surface * (id + 1) / (int)drawing_data->nb_thread; */
	init_canvas(canvas_begin, canvas_end, drawing_data->dragon, -1);
	pthread_barrier_wait(drawing_data->barrier);


	/* 2. Dessiner les dragons dans les 4 directions
	 *
	 * Il est attendu que chaque threads dessine une partie
	 * de chaque dragon.
	 * */

	int nb    = drawing_data->size / drawing_data->nb_thread;
	int start = id * nb;
	int end   = nb * (id + 1);
	/* int end   = (id == drawing_data->nb_thread - 1) ? */ 
	/* 	drawing_data->size : nb * (id + 1); */

	printf_threadsafe("TID = %d. Range = [%d ; %d]. Id = %d.\n", gettid(), start, end, id);

	int i;
	for (i = 0; i < NB_TILES; ++i) {
		dragon_draw_raw(i, start, end, drawing_data->dragon, 
				width, height, drawing_data->limits, id, 1);
	}

	pthread_barrier_wait(drawing_data->barrier);


	/* 3. Effectuer le rendu final */

	int nbRender    = drawing_data->image_height / drawing_data->nb_thread;
	int startRender = id * nbRender;
	int endRender   = nbRender * (id + 1);
	/* int endRender   = (id == drawing_data->nb_thread - 1)  ? */ 
	/* 	drawing_data->image_height : nbRender * (id + 1); */
	
	scale_dragon(startRender, endRender, drawing_data->image, drawing_data->image_width,
			drawing_data->image_height, drawing_data->dragon, width,
			height, drawing_data->palette);

	return NULL;
}

int dragon_draw_pthread(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	/* TODO("dragon_draw_pthread"); */

	pthread_t *threads = NULL;
	pthread_barrier_t barrier;
	limits_t lim;
	struct draw_data info;
	char *dragon = NULL;
	int scale_x;
	int scale_y;
	struct draw_data *data = NULL;
	struct palette *palette = NULL;
	int ret = 0;

	palette = init_palette(nb_thread);
	if (palette == NULL)
		goto err;

	/* 1. Initialiser barrier. */

	if (pthread_barrier_init(&barrier, NULL, nb_thread) != 0)
		goto err;

	if (dragon_limits_pthread(&lim, size, nb_thread) < 0)
		goto err;

	info.dragon_width = lim.maximums.x - lim.minimums.x;
	info.dragon_height = lim.maximums.y - lim.minimums.y;

	if ((dragon = (char *) malloc(info.dragon_width * info.dragon_height)) == NULL) {
		printf("malloc error dragon\n");
		goto err;
	}

	if ((data = malloc(sizeof(struct draw_data) * nb_thread)) == NULL) {
		printf("malloc error data\n");
		goto err;
	}

	if ((threads = malloc(sizeof(pthread_t) * nb_thread)) == NULL) {
		printf("malloc error threads\n");
		goto err;
	}

	info.image_height = height;
	info.image_width = width;
	scale_x = info.dragon_width / width + 1;
	scale_y = info.dragon_height / height + 1;
	info.scale = (scale_x > scale_y ? scale_x : scale_y);
	info.deltaJ = (info.scale * width - info.dragon_width) / 2;
	info.deltaI = (info.scale * height - info.dragon_height) / 2;
	info.nb_thread = nb_thread;
	info.dragon = dragon;
	info.image = image;
	info.size = size;
	info.limits = lim;
	info.barrier = &barrier;
	info.palette = palette;

	/* 2. Lancement du calcul parallèle principal avec dragon_draw_worker */

	int i;
	for (i = 0; i < nb_thread; i++) {
		info.id = i;
		data[i] = info;
		if (pthread_create(&threads[i], NULL, dragon_draw_worker, (void *)&data[i]) != 0)
			goto err;
	}

	/* 3. Attendre la fin du traitement */

	for (i = 0; i < nb_thread; i++) {
		pthread_join(threads[i], NULL);	
	}

	/* 4. Destruction des variables. */
	if (pthread_barrier_destroy(&barrier) != 0)
		goto err;

done:
	FREE(data);
	FREE(threads);
	free_palette(palette);
	// *canvas = dragon;
	*canvas = NULL; // TODO: retourner le dragon calculé
	return ret;

err:
	FREE(dragon);
	ret = -1;
	goto done;
}

void *dragon_limit_worker(void *data)
{
	int i;
	struct limit_data *lim = (struct limit_data *) data;
	int start = lim->start;
	int end = lim->end;

	for (i = 0; i < NB_TILES; i++) {
		piece_limit(start, end, &lim->pieces[i]);
	}

	return NULL;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_pthread(limits_t *limits, uint64_t size, int nb_thread)
{
	/* TODO("dragon_limits_pthread"); */

	int ret = 0;
	int i;
	pthread_t *threads = NULL;
	struct limit_data *thread_data = NULL;
	piece_t masters[NB_TILES];

	for (i = 0; i < NB_TILES; i++) {
		/**
		 * La pièce master représente les limites d'un dragon complet.
		 * Notez bien que chaque dragon à une orientation différente.
		 */
		piece_init(&masters[i]);
		masters[i].orientation = tiles_orientation[i];
	}

	/* 1. Allouer de l'espace pour threads et threads_data. */

	threads     = (pthread_t *)malloc(nb_thread * sizeof(pthread_t));
	thread_data = (struct limit_data *)malloc(nb_thread * sizeof(struct limit_data));

	/* 2. Lancement du calcul en parallèle avec dragon_limit_worker. */

	for (int i = 0; i < nb_thread; i++) {

		thread_data[i].id    = i;
		thread_data[i].start = i * (size / nb_thread);
		thread_data[i].end   = (i == nb_thread - 1) ? size : (i+1) * (size / nb_thread);
		for (int j = 0; j < NB_TILES; j++) {
			/* thread_data[i].pieces[j] = masters[j]; */
			piece_init(&thread_data[i].pieces[j]);
			thread_data[i].pieces[j].orientation = tiles_orientation[j];
		}
		if (pthread_create(&threads[i], NULL, dragon_limit_worker, (void *)&thread_data[i]) != 0)
			goto err;
	}


	/* 3. Attendre la fin du traitement. */
	for (int i = 0; i < nb_thread; i++) 
		pthread_join(threads[i], NULL);
	

	/* 4. Fusion des pièces.
	 *
	 * La fonction piece_merge est disponible afin d'accomplir ceci.
	 * Notez bien que les pièces ayant la même orientation initiale
	 * doivent être fusionnées ensemble.
	 * */

	for (int i = 0; i < nb_thread; i++) {
		for (int j = 0; j < NB_TILES; j++) {
			piece_merge(&masters[j], thread_data[i].pieces[j], tiles_orientation[j]);			
		}
	}

	/* La limite globale est calculée à partir des limites
	 * de chaque dragon calculées à l'étape 4.
	 */
	merge_limits(&masters[0].limits, &masters[1].limits);
	merge_limits(&masters[0].limits, &masters[2].limits);
	merge_limits(&masters[0].limits, &masters[3].limits);
	/* for (int i = 1; i < NB_TILES; i++) { */
	/* 	merge_limits(&masters[0].limits, &masters[i].limits); */
	/* } */

done:
	FREE(threads);
	FREE(thread_data);
	*limits = masters[0].limits;
	return ret;
err:
	ret = -1;
	goto done;
}
