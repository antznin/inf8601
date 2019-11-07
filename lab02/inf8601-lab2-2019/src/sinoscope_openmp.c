/*
 * sinoscope_openmp.c
 * 
 *  Created on: 2011-10-14
 *      Author: francis
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "sinoscope.h"
#include "color.h"
#include "util.h"

int sinoscope_image_openmp(sinoscope_t *ptr)
{
    if (ptr == NULL)
        return -1;

    sinoscope_t sino = *ptr;
    int x, y, taylor, index;
    struct rgb c;
    float val, px, py;
    //les variables suivante servent a eviter les acces a repetitions aux variables de la structure qui ne peuvent êtremises dans des registres car elles ne sont pas locales
    unsigned char *buf = sino.buf;
    float dx = sino.dx, dy = sino.dy, phase0 = sino.phase0, phase1 = sino.phase1, time = sino.time, interval = sino.interval, interval_inv = sino.interval_inv;
    int sinotaylor = sino.taylor;
    int width = sino.width, height = sino.height;
    // On choisit un schedule static car a priori les differentes parties de la boucle representent autant de travail et ca nous evite de perdre du temps a les repartir dynamiquement
    // il faut aussi faire tres attention a mettre la structure c en private, sinon les threads la partagent et la modifient pendant qu'un autre thread est entrain de colorien, ce qui cree des imperfections dans l'image.
    #pragma omp parallel for private(px, py, val, x,y, taylor, c) shared (dx, dy, phase0, phase1, time, interval, interval_inv, sinotaylor, width, height) schedule(static)
    for (x = 1; x <  width-1; x ++){
        for (y = 1; y < height-1 ;y++){
            px = dx * y - 2 * M_PI;
            py = dy * x - 2 * M_PI;
            val = 0.0f;
            for (taylor = 1; taylor <= sinotaylor; taylor += 2) {
                val += sin(px * taylor * phase1 + time) / taylor + cos(py * taylor * phase0) / taylor;
            }
            val = (atan(1.0 * val) - atan(-1.0 * val)) / (M_PI);
            val = (val + 1) * 100;
            value_color(&c, val, interval, interval_inv);
            index = (y * 3) + (x * 3) * width;
            buf[index + 0] = c.r;
            buf[index + 1] = c.g;
            buf[index + 2] = c.b;

        }
    }
    return 0;
}
