/*
 * This file is part of the libomnidrive project.
 *
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>,
 *                    Ingo Kresse <kresse@in.tum.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/* DESIGN PHASE 1: 
 * - The program is REQUIRED to call omnidrive_odometry often to ensure
 *   reasonable precision.
 */


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <math.h>

#include "omnilib.h"
#include "realtime.h" // defines omniread_t, omniwrite_t
#include <ecrt.h>  //part of igh's ethercat master


/*****************************************************************************/
/* global variables                       */


// Calculated for the APM-SC05-ADK9 motors with 8" HD AndyMark wheels
double odometry_constant=626594.7934;  // in ticks/m
double drive_constant=626594.7934;
double odometry_correction=1.0;
//int max_tick_speed = 666666; // ticks/s : 4000 rpm/ 60s * 10000 ticks/rev
int max_tick_speed = 833333; // ticks/s : 5000 rpm/ 60s * 10000 ticks/rev

int odometry_initialized = 0;
int32_t last_odometry_position[NUM_DRIVES]={0, 0, 0, 0, 0};
double odometry[3] = {0, 0, 0};

int status[NUM_DRIVES];
commstatus_t commstatus;

void omnidrive_speedcontrol();
void configure_torso_drive();

double static old_torso_pos = 0.0;

int omnidrive_init(void)
{
  printf("---- omnidrive_init ---- \n");
  int counter=0;



  if(!start_omni_realtime(max_tick_speed))
    return -1;

  omnidrive_poweroff();

  omniread_t cur = omni_read_data();

  for (counter = 0; counter < 200; counter ++) {
    printf("---------working_counter: %d\n", cur.working_counter_state);
    if (cur.working_counter_state >= 2)
      break;

    usleep(100000);
    cur = omni_read_data();
  }

  printf("After init, working_counter_state = %d\n", cur.working_counter_state);
  printf("After init, working_counter = %d\n", cur.working_counter);

  if (cur.working_counter_state < 2) {// failed to initialize modules...
    printf("cur.working_counter_state = %d\n", cur.working_counter_state);
    return -1;
  }

  omnidrive_speedcontrol();
  configure_torso_drive();
  omnidrive_recover();
  omnidrive_poweron();

  printf("Returning happy\n");
  return 0;
}


int omnidrive_shutdown(void)
{
  omnidrive_drive(0, 0, 0, 0);   /* SAFETY */  //FIXME: 0 as goal torso pos might be wrong

  omnidrive_poweroff();

  stop_omni_realtime();

  return 0;
}


/*! Jacobians for a mecanum wheels based omnidirectional platform.
 *  The functions jac_forward and jac_inverse convert cartesian
 *  velocities into wheel velocities and vice versa.
 *  For our motors, the order and signs are changed. The matrix C accounts
 *  for this.
 */

void jac_forward(double *in, double *out)
{
  // computing:
  //   out = (C*J_fwd) * in
  // with:
  //   J_fwd = [1 -1 -alpha;
  //            1  1  alpha;
  //            1  1 -alpha;
  //            1 -1  alpha]
  //   C     = [0  0  0  1;
  //            0  0 -1  0;
  //            0  1  0  0;
  //           -1  0  0  0 ]

  int i,j;
//#define alpha (0.3425 + 0.24)
#define alpha (0.39225 + 0.303495)
  double C_J_fwd[4][3] = {{ 1, -1, alpha},
                          {-1, -1, alpha},
                          { 1,  1, alpha},
                          {-1,  1, alpha}};

  // assert(in  != 0);
  // assert(out != 0);

  for(i=0; i < 4; i++) {
    out[i] = 0;
    for(j=0; j < 3; j++) {
      out[i] += C_J_fwd[i][j]*in[j];
    }
  }
}


void jac_inverse(double *in, double *out)
{
  // computing:
  //   out = (J_inv*C^-1) * in
  // with:
  //   J_fwd = 1/4*[1 -1 -alpha;
  //                1  1  alpha;
  //                1  1 -alpha;
  //                1 -1  alpha]
  //   C = [0  0  0  1;
  //        0  0 -1  0;
  //        0  1  0  0;
  //       -1  0  0  0 ]

  int i,j;
//#define alpha (0.3425 + 0.24)
#define alpha (0.39225 + 0.303495)
  double J_inv_C[3][4] = {{ 0.25,      -0.25,       0.25,      -0.25},
                          {-0.25,      -0.25,       0.25,       0.25},
                          { 0.25/alpha, 0.25/alpha, 0.25/alpha, 0.25/alpha}};

  // assert(in  != 0);
  // assert(out != 0);

  for(i=0; i < 3; i++) {
    out[i] = 0;
    for(j=0; j < 4; j++) {
      out[i] += J_inv_C[i][j]*in[j];
    }
  }
}


int omnidrive_drive(double x, double y, double a, double torso_pos)
{
  // speed limits for the robot
  double wheel_limit = 1.0 ;//0.8;  // a single wheel may drive this fast (m/s)
  double cart_limit = 0.5 ; //0.5;   // any point on the robot may move this fast (m/s)
  double radius = 0.7;       // (maximum) radius of the robot (m)

  // 0.5 m/s is 1831 ticks. kernel limit is 2000 ticks.

  double corr_wheels, corr_cart, corr;

  omniwrite_t tar;
  memset(&tar, 0, sizeof(tar));

  double cartesian_speeds[3] = {x, y, a}, wheel_speeds[4];
  int i;

  //TODO: check if the robot is up. if not, return immediately
  
  tar.magic_version = OMNICOM_MAGIC_VERSION;

  // check for limits

  // cartesian limit: add linear and angular parts
  corr_cart = cart_limit / (sqrt(x*x + y*y) + radius*fabs(a));

  // wheel limit: for one wheel, x,y and a always add up
  corr_wheels = wheel_limit / (fabs(x) + fabs(y) + fabs(a));

  // get limiting factor as min(1, corr_cart, corr_wheels)
  corr = (1 < corr_cart) ? 1 : ((corr_cart < corr_wheels) ? corr_cart : corr_wheels);

  jac_forward(cartesian_speeds, wheel_speeds);

  for(i = 0; i < 4; i++) {
    //FIXME: scale the entire twist and NOT the wheels with a correction factor
    tar.target_velocity[i] = wheel_speeds[i] * corr * drive_constant;
    //tar.torque_set_value[i] = 0.0;
  }

  //torso
  if (old_torso_pos != torso_pos){
      printf("omnilib-> new_torso_pos = %f\n", torso_pos);
      tar.target_position[TORSO_DRIVE_SEQ] = torso_pos;
      tar.send_new_torso_pos = 1;
  } else {
      tar.target_position[TORSO_DRIVE_SEQ] = old_torso_pos;
      tar.send_new_torso_pos = 0;
  }

  old_torso_pos = torso_pos;

  tar.profile_velocity[TORSO_DRIVE_SEQ] = 250000;
  tar.profile_acceleration[TORSO_DRIVE_SEQ] = 1000000;
  tar.profile_deceleration[TORSO_DRIVE_SEQ] = 1000000;



  /* Let the kernel know the velocities we want to set. */
  omni_write_data(tar);

  return 0;
}

void omnidrive_set_correction(double drift)
{
  odometry_correction = drift;
}

int omnidrive_odometry(double *x, double *y, double *a, double *torso_pos)
{
  omniread_t cur; /* Current velocities / torques / positions */
  int i;
  double d_wheel[4], d[3], ang;

  /* Read data from kernel module. */
  cur = omni_read_data();

  // copy status values
  for(i=0; i < NUM_DRIVES; i++)
    status[i] = cur.status[i];

  for(i=0; i < NUM_DRIVES; i++) {
    commstatus.slave_state[i] = cur.slave_state[i];
    commstatus.slave_online[i] = cur.slave_online[i];
    commstatus.slave_operational[i] = cur.slave_operational[i];
  }
  commstatus.master_link = cur.master_link;
  commstatus.master_al_states = cur.master_al_states;
  commstatus.master_slaves_responding = cur.master_slaves_responding;
  commstatus.working_counter = cur.working_counter;
  commstatus.working_counter_state = cur.working_counter_state;


  /* start at (0, 0, 0) */
  if(!odometry_initialized) {
    for (i = 0; i < NUM_DRIVES; i++)
      last_odometry_position[i] = cur.position[i];
    odometry_initialized = 1;
  }

  /* compute differences of encoder readings and convert to meters */
  for (i = 0; i < 4; i++) {
    d_wheel[i] = (int) (cur.position[i] - last_odometry_position[i]) * (1.0/(odometry_constant*odometry_correction));
    /* remember last wheel position */
    last_odometry_position[i] = cur.position[i];
  }

  /* IMPORTANT: Switch order of the motor numbers! 
     --> moved to jacobian_inverse */

  jac_inverse(d_wheel, d);
    
  ang = odometry[2] + d[2]/2.0;

  //FIXME: Inverted the commands, also invert the readings
  odometry[0] += -1 * (d[0]*cos(ang) - d[1]*sin(ang));
  odometry[1] += -1 * (d[0]*sin(ang) + d[1]*cos(ang));
  odometry[2] += -1 * d[2];


  /* return current odometry values */
  *x = odometry[0];
  *y = odometry[1];
  *a = odometry[2];
  *torso_pos = (double)cur.position[TORSO_DRIVE_SEQ] / 10000000.0;

  return 0;
}

int readSDO(int device, int objectNum)
{
  FILE *fp;
  char cmd[1024], res[1024];
  char *redundant;
  sprintf(cmd, "ethercat upload -p %d --type uint16 0x%x 0 |awk '{print $2}'", device, objectNum);

  fp = popen(cmd, "r");
  redundant = fgets(res, sizeof(res), fp);
  pclose(fp);

  return atol(res);
}


//This order mus match *types below
#define INT8   0
#define UINT8  1
#define INT16  2
#define UINT16 3
#define INT32  4
#define UINT32 5


int writeSDO(int device, int objectNum, int value, int type)
{
    return(writeSDO_lib(device, objectNum, 0, value, type));
}


int writeSDO_old(int device, int objectNum, int value, int type)
{
  char cmd[1024];
  char *types[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32"};
  sprintf(cmd, "ethercat download -p %d --type %s -- 0x%x 0 %d", device, types[type], objectNum, value);
  return system(cmd);
}

int writeSDO_lib(int device, int index, int subindex, int value, int type)
{

    //get the point to the ethercat master from the realtime section
    ec_master_t* master = get_master();

    //abort if the master is not initialized
    if (master == NULL) {
        printf("Tried to use NULL master. index=0x%0x value=0x%0x\n", index, value);
        return(-1);

    }

    //find the size of the variable to transmit
    int data_size = 0;
    switch (type) {
      case INT8:
      case UINT8:
        data_size = 1;
        break;
      case INT16:
      case UINT16:
        data_size = 2;
        break;
      case INT32:
      case UINT32:
        data_size = 4;
        break ;
    }

    uint8_t *data = &value;

    //send the SDO (blocking call)
    int ret = ecrt_master_sdo_download(master, device, index, subindex, data, data_size, 0);

    return(ret);

}

void drive_status(char *drive, int index)
{
  int i;
  char statusdisp[] = { '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  'E',  'F'};
  int  statuscode[] = {0x00, 0x40, 0x21, 0x33, 0x37, 0xff, 0xff, 0x17, 0x0f, 0x08};
  int  statusmask[] = {0x5f, 0x4f, 0x6f, 0x7f, 0x7f, 0x00, 0x00, 0x7f, 0x4f, 0x4f};
  if(drive)
    for(i=0; i < 10; i++)
      if((status[index] & statusmask[i]) == statuscode[i])
        *drive = statusdisp[i];
}

commstatus_t omnidrive_commstatus()
{
  return commstatus;
}

void omnidrive_status(char *drive0, char *drive1, char *drive2, char *drive3, char *drive4, int *estop)
{
  drive_status(drive0, 0);
  drive_status(drive1, 1);
  drive_status(drive2, 2);
  drive_status(drive3, 3);
  drive_status(drive4, 4); //torso

  if(estop)
    *estop = 0x80 & (status[0] & status[1] & status[2] & status[3] & status[4]); //added one for the torso
}

void omnidrive_recover()
{
  writeSDO(0, 0x6040, 0x80, UINT16);
  writeSDO(1, 0x6040, 0x80, UINT16);
  writeSDO(2, 0x6040, 0x80, UINT16);
  writeSDO(3, 0x6040, 0x80, UINT16);
  writeSDO(4, 0x6040, 0x80, UINT16);
}

void omnidrive_poweron()
{
  writeSDO(0, 0x6040, 0x06, UINT16);
  writeSDO(1, 0x6040, 0x06, UINT16);
  writeSDO(2, 0x6040, 0x06, UINT16);
  writeSDO(3, 0x6040, 0x06, UINT16);
  writeSDO(4, 0x6040, 0x06, UINT16);  //torso

  writeSDO(0, 0x6040, 0x07, UINT16);
  writeSDO(1, 0x6040, 0x07, UINT16);
  writeSDO(2, 0x6040, 0x07, UINT16);
  writeSDO(3, 0x6040, 0x07, UINT16);
  writeSDO(4, 0x6040, 0x07, UINT16);  //torso

  writeSDO(0, 0x6040, 0x0f, UINT16);
  writeSDO(1, 0x6040, 0x0f, UINT16);
  writeSDO(2, 0x6040, 0x0f, UINT16);
  writeSDO(3, 0x6040, 0x0f, UINT16);
  writeSDO(4, 0x6040, 0x0f, UINT16);  //torso

}

void omnidrive_poweroff()
{
  writeSDO(0, 0x6040, 0x00, UINT16);
  writeSDO(1, 0x6040, 0x00, UINT16);
  writeSDO(2, 0x6040, 0x00, UINT16);
  writeSDO(3, 0x6040, 0x00, UINT16);
  writeSDO(4, 0x6040, 0x00, UINT16); //torso
}

void omnidrive_speedcontrol()
{
  writeSDO(0, 0x6060, 3, INT8); //3 = Velocity profile mode
  writeSDO(1, 0x6060, 3, INT8);
  writeSDO(2, 0x6060, 3, INT8);
  writeSDO(3, 0x6060, 3, INT8);


}

void configure_torso_drive()
{
    //set the torso drive to velocity profile mode, and good default values


    writeSDO(TORSO_DRIVE_SEQ, 0x6081, 200000, UINT32);  //decent profile speed
    writeSDO(TORSO_DRIVE_SEQ, 0x6083, 10000000, UINT32);  //profile acceleration
    writeSDO(TORSO_DRIVE_SEQ, 0x6084, 10000000, UINT32);  //profile deceleration
    writeSDO(TORSO_DRIVE_SEQ, 0x6085, 10000000, UINT32);  //quick stop deceleration
    writeSDO(TORSO_DRIVE_SEQ, 0x6086, 0, INT16);  //motion profile type = 0
    writeSDO(TORSO_DRIVE_SEQ, 0x6060, 1, INT8);  //mode of operation = 1 = profile position mode


}


void start_home_torso_drive()
{
    //set the torso drive to velocity profile mode, and good default values
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x6099, 1, 200000, UINT32);     //home search speed
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x6099, 2, 20000, UINT32);      //home search slow speed
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x609A, 0, 10000000, UINT32);   //deceleration
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x6098, 0, 2, INT8);            //homing method
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x607C, 0, 0, INT32);           //home offset to zero
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x6060, 0, 6, INT8);            //mode of operation to 6 (homing)
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x6040, 0, 0x0f, UINT16);       //control word to 0x0f (bring up the drive)
    writeSDO_lib(TORSO_DRIVE_SEQ, 0x6040, 0, 0x1f, UINT16);       //control word to 0x1f (start the homing)

    //After the homing is finished, switch back to mode 1 (profiled position)
    //writeSDO_lib(TORSO_DRIVE_SEQ, 0x6060, 0, 1, INT8);            //mode of operation to 1 (profiled position)
}

int homing_reached(int statusword)
{
    #define STATUSWORD_HOMING_ATTAINED_BIT 12
    if ((statusword & (1 << STATUSWORD_HOMING_ATTAINED_BIT))) {
        return (1);
    } else {
        return(0);
    }
}


