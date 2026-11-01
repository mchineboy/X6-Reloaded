/* Explicit registry for the five user-requested games; no broad driver list. */
#include "driver.h"
struct GameDriver driver_0={__FILE__,0,"",0,0,0,0,0,0,0,NOT_A_DRIVER};
extern struct GameDriver driver_dkong,driver_galaga,driver_digdug,driver_rygar,driver_altbeast;
const struct GameDriver *drivers[]={&driver_dkong,&driver_galaga,&driver_digdug,&driver_rygar,&driver_altbeast,0};
