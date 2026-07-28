/* savestates.c - STUB temporal
 * La implementacion real usa zip/zlib, que este build no tiene todavia.
 * Este stub deja el link resuelto y no hace nada (no hay slot pendiente
 * de guardar/cargar). El original quedo en savestates.c.orig_backup.
 */

#include "savestates.h"

static savestates_job pending_job;   /* 0 = primer valor del enum, tipicamente "nada pendiente" */
static unsigned int current_slot = 0;

savestates_job savestates_get_job(void)
{
        return pending_job;
}

void savestates_set_job(savestates_job j, savestates_type t, const char *fn)
{
        (void)t;
        (void)fn;
        pending_job = j;
}

void savestates_clear_job(void)
{
        pending_job = (savestates_job)0;
}

int savestates_load(void)
{
        return 0;
}

int savestates_save(void)
{
        return 0;
}

void savestates_select_slot(unsigned int s)
{
        current_slot = s;
}

unsigned int savestates_get_slot(void)
{
        return current_slot;
}

void savestates_set_autoinc_slot(int b)
{
        (void)b;
}

void savestates_inc_slot(void)
{
        current_slot++;
}
