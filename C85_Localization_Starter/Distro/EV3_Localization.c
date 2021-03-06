/*

  CSC C85 - Embedded Systems - Project # 1 - EV3 Robot Localization
  
 This file provides the implementation of all the functionality required for the EV3
 robot localization project. Please read through this file carefully, and note the
 sections where you must implement functionality for your bot. 
 
 You are allowed to change *any part of this file*, not only the sections marked
 ** TO DO **. You are also allowed to add functions as needed (which must also
 be added to the header file). However, *you must clearly document* where you 
 made changes so your work can be properly evaluated by the TA.

 NOTES on your implementation:

 * It should be free of unreasonable compiler warnings - if you choose to ignore
   a compiler warning, you must have a good reason for doing so and be ready to
   defend your rationale with your TA.
 * It must be free of memory management errors and memory leaks - you are expected
   to develop high wuality, clean code. Test your code extensively with valgrind,
   and make sure its memory management is clean.
 
 In a nutshell, the starter code provides:
 
 * Reading a map from an input image (in .ppm format). The map is bordered with red, 
   must have black streets with yellow intersections, and buildings must be either
   blue, green, or be left white (no building).
   
 * Setting up an array with map information which contains, for each intersection,
   the colours of the buildings around it in ** CLOCKWISE ** order from the top-left.
   
 * Initialization of the EV3 robot (opening a socket and setting up the communication
   between your laptop and your bot)
   
 What you must implement:
 
 * All aspects of robot control:
   - Finding and then following a street
   - Recognizing intersections
   - Scanning building colours around intersections
   - Detecting the map boundary and turning around or going back - the robot must not
     wander outside the map (though of course it's possible parts of the robot will
     leave the map while turning at the boundary)

 * The histogram-based localization algorithm that the robot will use to determine its
   location in the map - this is as discussed in lecture.

 * Basic robot exploration strategy so the robot can scan different intersections in
   a sequence that allows it to achieve reliable localization
   
 * Basic path planning - once the robot has found its location, it must drive toward a 
   user-specified position somewhere in the map.

 --- OPTIONALLY but strongly recommended ---
 
  The starter code provides a skeleton for implementing a sensor calibration routine,
 it is called when the code receives -1  -1 as target coordinates. The goal of this
 function should be to gather informatin about what the sensor reads for different
 colours under the particular map/room illumination/battery level conditions you are
 working on - it's entirely up to you how you want to do this, but note that careful
 calibration would make your work much easier, by allowing your robot to more
 robustly (and with fewer mistakes) interpret the sensor data into colours. 
 
   --> The code will exit after calibration without running localization (no target!)
       SO - your calibration code must *save* the calibration information into a
            file, and you have to add code to main() to read and use this
            calibration data yourselves.
   
 What you need to understand thoroughly in order to complete this project:
 
 * The histogram localization method as discussed in lecture. The general steps of
   probabilistic robot localization.

 * Sensors and signal management - your colour readings will be noisy and unreliable,
   you have to handle this smartly
   
 * Robot control with feedback - your robot does not perform exact motions, you can
   assume there will be error and drift, your code has to handle this.
   
 * The robot control API you will use to get your robot to move, and to acquire 
   sensor data. Please see the API directory and read through the header files and
   attached documentation
   
 Starter code:
 F. Estrada, 2018 - for CSC C85 
 
*/

#include "EV3_Localization.h"

int map[400][4];            // This holds the representation of the map, up to 20x20
                            // intersections, raster ordered, 4 building colours per
                            // intersection.
int sx, sy;                 // Size of the map (number of intersections along x and y)
double beliefs[400][4];     // Beliefs for each location and motion direction
int init_angle;
int past_angle;
int isRotating;

int main(int argc, char *argv[])
{
 char mapname[1024];
 int dest_x, dest_y, rx, ry;
 unsigned char *map_image;
 
 memset(&map[0][0],0,400*4*sizeof(int));
 sx=0;
 sy=0;
 
 if (argc<4)
 {
  fprintf(stderr,"Usage: EV3_Localization map_name dest_x dest_y\n");
  fprintf(stderr,"    map_name - should correspond to a properly formatted .ppm map image\n");
  fprintf(stderr,"    dest_x, dest_y - target location for the bot within the map, -1 -1 calls calibration routine\n");
  exit(1);
 }
 strcpy(&mapname[0],argv[1]);
 dest_x=atoi(argv[2]);
 dest_y=atoi(argv[3]);

 if (dest_x==-1&&dest_y==-1)
 {
  calibrate_sensor();
  exit(1);
 }

 /******************************************************************************************************************
  * OPTIONAL TO DO: If you added code for sensor calibration, add just below this comment block any code needed to
  *   read your calibration data for use in your localization code. Skip this if you are not using calibration
  * ****************************************************************************************************************/
//  FILE *fptr;
//  fptr = fopen("calibrated_colors.txt", "r");
//  fscanf(fptr, "%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n", &my_RED[0], &my_RED[1], &my_RED[2], &my_GREEN[0], &my_GREEN[1], &my_GREEN[2], &my_BLUE[0], &my_BLUE[1], &my_BLUE[2], &my_BLACK[0], &my_BLACK[1], &my_BLACK[2], &my_YELLOW[0], &my_YELLOW[1], &my_YELLOW[2], &my_WHITE[0], &my_WHITE[1], &my_WHITE[2]);
//  fclose(fptr);
 
 // Your code for reading any calibration information should not go below this line //
 
 map_image=readPPMimage(&mapname[0],&rx,&ry);
 if (map_image==NULL)
 {
  fprintf(stderr,"Unable to open specified map image\n");
  exit(1);
 }
 
 if (parse_map(map_image, rx, ry)==0)
 { 
  fprintf(stderr,"Unable to parse input image map. Make sure the image is properly formatted\n");
  free(map_image);
  exit(1);
 }

 if (dest_x<0||dest_x>=sx||dest_y<0||dest_y>=sy)
 {
  fprintf(stderr,"Destination location is outside of the map\n");
  free(map_image);
  exit(1);
 }

 // Initialize beliefs - uniform probability for each location and direction
 for (int j=0; j<sy; j++)
  for (int i=0; i<sx; i++)
  {
   beliefs[i+(j*sx)][0]=1.0/(double)(sx*sy*4);
   beliefs[i+(j*sx)][1]=1.0/(double)(sx*sy*4);
   beliefs[i+(j*sx)][2]=1.0/(double)(sx*sy*4);
   beliefs[i+(j*sx)][3]=1.0/(double)(sx*sy*4);
  }
  // printf("\n\n\n\n\nsx: %d, sy: %d\n", sx, sy);

  // printf("index %d", get_index(1,2));
  // printf("\n rx: %d, ry: %d\n", rx, ry);
  // for (int i = 0; i < 15; i++) {
  //   for (int j =0; j<4; j++) {
  //     printf("%d ", map[i][j]);
  //   }
  //     printf("\n ");
  // }

 // Open a socket to the EV3 for remote controlling the bot.
 if (BT_open(HEXKEY)!=0)
 {
  fprintf(stderr,"Unable to open comm socket to the EV3, make sure the EV3 kit is powered on, and that the\n");
  fprintf(stderr," hex key for the EV3 matches the one in EV3_Localization.h\n");
  free(map_image);
  exit(1);
 }

 fprintf(stderr,"All set, ready to go!\n");
 
/*******************************************************************************************************************************
 *
 *  TO DO - Implement the main localization loop, this loop will have the robot explore the map, scanning intersections and
 *          updating beliefs in the beliefs array until a single location/direction is determined to be the correct one.
 * 
 *          The beliefs array contains one row per intersection (recall that the number of intersections in the map_image
 *          is given by sx, sy, and that the map[][] array contains the colour indices of buildings around each intersection.
 *          Indexing into the map[][] and beliefs[][] arrays is by raster order, so for an intersection at i,j (with 0<=i<=sx-1
 *          and 0<=j<=sy-1), index=i+(j*sx)
 *  
 *          In the beliefs[][] array, you need to keep track of 4 values per intersection, these correspond to the belief the
 *          robot is at that specific intersection, moving in one of the 4 possible directions as follows:
 * 
 *          beliefs[i][0] <---- belief the robot is at intersection with index i, facing UP
 *          beliefs[i][1] <---- belief the robot is at intersection with index i, facing RIGHT
 *          beliefs[i][2] <---- belief the robot is at intersection with index i, facing DOWN
 *          beliefs[i][3] <---- belief the robot is at intersection with index i, facing LEFT
 * 
 *          Initially, all of these beliefs have uniform, equal probability. Your robot must scan intersections and update
 *          belief values based on agreement between what the robot sensed, and the colours in the map. 
 * 
 *          You have two main tasks these are organized into two major functions:
 * 
 *          robot_localization()    <---- Runs the localization loop until the robot's location is found
 *          go_to_target()          <---- After localization is achieved, takes the bot to the specified map location
 * 
 *          The target location, read from the command line, is left in dest_x, dest_y
 * 
 *          Here in main(), you have to call these two functions as appropriate. But keep in mind that it is always possible
 *          that even if your bot managed to find its location, it can become lost again while driving to the target
 *          location, or it may be the initial localization was wrong and the robot ends up in an unexpected place - 
 *          a very solid implementation should give your robot the ability to determine it's lost and needs to 
 *          run localization again.
 *
 *******************************************************************************************************************************/  

 // HERE - write code to call robot_localization() and go_to_target() as needed, any additional logic required to get the
 //        robot to complete its task should be here.
  int cx, cy, direction;
  // find_street();
  robot_localization(&cx, &cy, &direction);
  printf("%d, %d %d\n", cx, cy, direction);
  // turn_at_intersection(0);
  go_to_target(cx, cy, direction, dest_x, dest_y);

  // while(!go_to_target(cx, cy, direction, dest_x, dest_y)){
  //   robot_localization(&cx, &cy, &direction);
  // }
  // go_to_target(cx, cy, direction, dest_x, dest_y);

  // int val = go_to_target(0,4,2,2,0);
  // int val = verify_colors(0, 3, 2);
  // printf("did it fail? %d\n", val);

  

  // center_sensor();
  // find_street();

  // while (1) {
  //   int rgb[3];
  //   BT_read_colour_sensor_RGB(PORT_2, rgb);
  //   printf("%d %d %d %c\n", rgb[0], rgb[1], rgb[2], what_color(rgb));
  // }
  // turn_at_intersection(0);
  // turn_at_intersection(0);
  
  // int a[4];
  // scan_intersection(&a[0], &a[1], &a[2], &a[3]);
  // printf("%c %c %c %c \n", a[0], a[1], a[2], a[3]);
  // find_street();
  // drive_along_street();
  
  // isRotating = 1;
  // init_angle = get_angle();
  // past_angle = get_angle();
  // while (isRotating) {
  //   rotate_to(-180);
  // }

 // Cleanup and exit - DO NOT WRITE ANY CODE BELOW THIS LINE
 BT_close();
 free(map_image);
 exit(0);
}

int find_street(void)   
{
 /*
  * This function gets your robot onto a street, wherever it is placed on the map. You can do this in many ways, but think
  * about what is the most effective and reliable way to detect a street and stop your robot once it's on it.
  * 
  * You can use the return value to indicate success or failure, or to inform the rest of your code of the state of your
  * bot after calling this function
  */
  int rgb[3];
  BT_read_colour_sensor_RGB(PORT_2, rgb);

  while (what_color(rgb) == 'y') {
    BT_drive(MOTOR_A, MOTOR_D, 10);
    BT_read_colour_sensor_RGB(PORT_2, rgb);
  }
  while (what_color(rgb) != 'k') {
    while (what_color(rgb) == 'r') {
      BT_all_stop(1);
      isRotating = 1;
      init_angle = get_angle();
      past_angle = get_angle();
      int random_angle = (int)(30*rand()/RAND_MAX) + 165;
      while (isRotating) {
        rotate_to(random_angle);
      }
      while (what_color(rgb) == 'r'){
        BT_drive(MOTOR_A, MOTOR_D, 10);
        BT_read_colour_sensor_RGB(PORT_2, rgb);
      }
      BT_read_colour_sensor_RGB(PORT_2, rgb);
    }
    BT_drive(MOTOR_A, MOTOR_D, 10);
    BT_read_colour_sensor_RGB(PORT_2, rgb);
  }
  BT_all_stop(0);

  BT_read_colour_sensor_RGB(PORT_2, rgb);
  int seen_yellow = 0;
  
  while (1) {
    while(what_color(rgb) == 'k') {
      BT_drive(MOTOR_A, MOTOR_D, 10);
      BT_read_colour_sensor_RGB(PORT_2, rgb);
      if(what_color(rgb) != 'k'){
        for (int i = 0; i < 2; i++)
        {
          BT_drive(MOTOR_A, MOTOR_D, 10);
          BT_read_colour_sensor_RGB(PORT_2, rgb);
        }
      }
      if (seen_yellow && what_color(rgb) == 'y') {
        BT_all_stop(0);
        return 0;
      }

      if (what_color(rgb) == 'y') {
        seen_yellow = 1;
        while (what_color(rgb) == 'y') {
          BT_drive(MOTOR_A, MOTOR_D, 10);
          BT_read_colour_sensor_RGB(PORT_2, rgb);
        }
        if(what_color(rgb) != 'y'){
        for (int i = 0; i < 3; i++)
        {
          BT_drive(MOTOR_A, MOTOR_D, 10);
          BT_read_colour_sensor_RGB(PORT_2, rgb);
        }
      }
      }
    }
    BT_all_stop(0);
    if (what_color(rgb) == 'r') {
      isRotating = 1;
      init_angle = get_angle();
      past_angle = get_angle();

      while(isRotating) {
        rotate_to(175);
      }

      BT_read_colour_sensor_RGB(PORT_2, rgb);

      while (what_color(rgb) != 'k') {
        BT_drive(MOTOR_A, MOTOR_D, 10);
        BT_read_colour_sensor_RGB(PORT_2, rgb);
      }
      BT_all_stop(0);
      continue;
    }

    //reverse until black again
    while (what_color(rgb) != 'k') {
      BT_drive(MOTOR_A, MOTOR_D, -10);
      BT_read_colour_sensor_RGB(PORT_2, rgb);
    }
    for (int i = 0; i < 5; i++)
    {
      BT_drive(MOTOR_A, MOTOR_D, -10);
      BT_read_colour_sensor_RGB(PORT_2, rgb);
    }
    
    BT_all_stop(0);

    isRotating = 1;
    init_angle = get_angle();
    past_angle = get_angle();

    while(isRotating) {
      rotate_to(6);
      
    }
  }
  return(0);
}

int drive_along_street(void)
{
 /*
  * This function drives your bot along a street, making sure it stays on the street without straying to other pars of
  * the map. It stops at an intersection.
  * 
  * You can implement this in many ways, including a controlled (PID for example), a neural network trained to track and
  * follow streets, or a carefully coded process of scanning and moving. It's up to you, feel free to consult your TA
  * or the course instructor for help carrying out your plan.
  * 
  * You can use the return value to indicate success or failure, or to inform the rest of your code of the state of your
  * bot after calling this function.
  */
  int hit_red = 0;
  int rgb[3];
  int went_left = 0;
  BT_read_colour_sensor_RGB(PORT_2, rgb);

  while (what_color(rgb) == 'y') {
    BT_drive(MOTOR_A, MOTOR_D, 10);
    BT_read_colour_sensor_RGB(PORT_2, rgb);
  }
  for (int i = 0; i < 5; i++)
  {
    BT_drive(MOTOR_A, MOTOR_D, 10);
  }
  
  BT_all_stop(0);

  BT_read_colour_sensor_RGB(PORT_2, rgb);
  // int seen_yellow = 0;
  
  while (1) {
    while(what_color(rgb) == 'k') {
      BT_drive(MOTOR_A, MOTOR_D, 10);
      BT_read_colour_sensor_RGB(PORT_2, rgb);
      if(what_color(rgb) != 'k'){
        for (int i = 0; i < 3; i++)
        {
          BT_drive(MOTOR_A, MOTOR_D, 10);
          BT_read_colour_sensor_RGB(PORT_2, rgb);
        }
      }
      if (what_color(rgb) == 'y') {
        BT_all_stop(0);
        return hit_red;
      }
    }
    BT_all_stop(0);
    if (what_color(rgb) == 'r') {
      hit_red = 1;
      isRotating = 1;
      init_angle = get_angle();
      past_angle = get_angle();

      while(isRotating) {
        rotate_to(170); //changed this
      }

      BT_read_colour_sensor_RGB(PORT_2, rgb);

      while (what_color(rgb) != 'k') {
        BT_drive(MOTOR_A, MOTOR_D, 10);
        BT_read_colour_sensor_RGB(PORT_2, rgb);
      }
      for (int i = 0; i < 5; i++)
      {
        BT_drive(MOTOR_A, MOTOR_D, 10);
      }
      BT_all_stop(0);
      continue;
    }

    //reverse until black again
    while (what_color(rgb) != 'k') {
      BT_drive(MOTOR_A, MOTOR_D, -10);
      BT_read_colour_sensor_RGB(PORT_2, rgb);
    }
    for (int i = 0; i < 10; i++)
    {
      BT_drive(MOTOR_A, MOTOR_D, -10);
    }
    BT_all_stop(0);

    isRotating = 1;
    init_angle = get_angle();
    past_angle = get_angle();

    if (went_left) {
      while(isRotating) {
        rotate_to(16);
      }
      went_left = !went_left;
      continue;
    }

    if (!went_left) {
      went_left = !went_left;

      while(isRotating) {
        rotate_to(-6);
      }
    }    
  }
  return(hit_red);
}

int scan_intersection(int *tl, int *tr, int *br, int *bl)
{
 /*
  * This function carries out the intersection scan - the bot should (obviously) be placed at an intersection for this,
  * and the specific set of actions will depend on how you designed your bot and its sensor. Whatever the process, you
  * should make sure the intersection scan is reliable - i.e. the positioning of the sensor is reliably over the buildings
  * it needs to read, repeatably, and as the robot moves over the map.
  * 
  * Use the APIs sensor reading calls to poll the sensors. You need to remember that sensor readings are noisy and 
  * unreliable so * YOU HAVE TO IMPLEMENT SOME KIND OF SENSOR / SIGNAL MANAGEMENT * to obtain reliable measurements.
  * 
  * Recall your lectures on sensor and noise management, and implement a strategy that makes sense. Document your process
  * in the code below so your TA can quickly understand how it works.
  * 
  * Once your bot has read the colours at the intersection, it must return them using the provided pointers to 4 integer
  * variables:
  * 
  * tl - top left building colour
  * tr - top right building colour
  * br - bottom right building colour
  * bl - bottom left building colour
  * 
  * The function's return value can be used to indicate success or failure, or to notify your code of the bot's state
  * after this call.
  */
 
  /************************************************************************************************************************
   *   TO DO  -   Complete this function
   ***********************************************************************************************************************/

 // Return invalid colour values, and a zero to indicate failure (you will replace this with your code)
 *(tl)=-1;
 *(tr)=-1;
 *(br)=-1;
 *(bl)=-1;
 
 int rgb[3];
 int motor_power = 5;
int color_buffer = 5;
 int out_color_buffer = 12;
 BT_read_colour_sensor_RGB(PORT_2, rgb);

//drive forward
 while (what_color(rgb) != 'k') {
   BT_drive(MOTOR_A, MOTOR_D, 10);
   BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<15;i++){
   BT_drive(MOTOR_A, MOTOR_D, 10);
 }
 BT_all_stop(0);

 //scan left
 while (what_color(rgb) == 'k') {
  BT_motor_port_start(MOTOR_C, motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
  // printf("%c %d %d %d\n",what_color(rgb), rgb[0], rgb[1],rgb[2]);
 } 
 for(int i=0;i<out_color_buffer;i++){
   BT_motor_port_start(MOTOR_C, motor_power);
 }
//  BT_motor_port_stop(MOTOR_C, 0);
 BT_all_stop(0);
 BT_read_colour_sensor_RGB(PORT_2, rgb);
 *(tl) = what_color(rgb);

//recenter
 while (what_color(rgb) != 'k') {
  BT_motor_port_start(MOTOR_C, -motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<color_buffer;i++){
   BT_motor_port_start(MOTOR_C, -motor_power);
 }
 BT_all_stop(0);

//scan right
 while (what_color(rgb) == 'k') {
  BT_motor_port_start(MOTOR_C, -motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<out_color_buffer;i++){
   BT_motor_port_start(MOTOR_C, -motor_power);
 }
 BT_all_stop(0);
 BT_read_colour_sensor_RGB(PORT_2, rgb);
 *(tr) = what_color(rgb);

 //recenter
 while (what_color(rgb) != 'k') {
  BT_motor_port_start(MOTOR_C, motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<color_buffer;i++){
   BT_motor_port_start(MOTOR_C, motor_power);
 }
 BT_all_stop(0);

 //move back
 while (what_color(rgb) != 'y') {
  BT_drive(MOTOR_A, MOTOR_D, -10);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<5;i++){
   BT_drive(MOTOR_A, MOTOR_D, -10);
 }
 BT_all_stop(0);


 //move back
 while (what_color(rgb) != 'k') {
  BT_drive(MOTOR_A, MOTOR_D, -10);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<15;i++){
   BT_drive(MOTOR_A, MOTOR_D, -10);
 }
 BT_all_stop(0);
 
 //scan left
 while (what_color(rgb) == 'k') {
  BT_motor_port_start(MOTOR_C, motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 } 
 for(int i=0;i<out_color_buffer;i++){
   BT_motor_port_start(MOTOR_C, motor_power);
 }
  BT_all_stop(0);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
  *(bl) = what_color(rgb);

//recenter
 while (what_color(rgb) != 'k') {
  BT_motor_port_start(MOTOR_C, -motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<color_buffer;i++){
   BT_motor_port_start(MOTOR_C, -motor_power);
 } 
 BT_all_stop(0);

//scan right
 while (what_color(rgb) == 'k') {
  BT_motor_port_start(MOTOR_C, -motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<out_color_buffer;i++){
   BT_motor_port_start(MOTOR_C, -motor_power);
 }
 BT_all_stop(0);
 *(br) = what_color(rgb);

 //recenter
 while (what_color(rgb) != 'k') {
  BT_motor_port_start(MOTOR_C, motor_power);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<color_buffer;i++){
   BT_motor_port_start(MOTOR_C, motor_power);
 }
 BT_all_stop(0);

 //drive forward
 while (what_color(rgb) != 'y') {
  BT_drive(MOTOR_A, MOTOR_D, 10);
  BT_read_colour_sensor_RGB(PORT_2, rgb);
 }
 for(int i=0;i<5;i++){
   BT_drive(MOTOR_A, MOTOR_D, 10);
 }
 BT_all_stop(0);
 center_sensor();

 return(0); 
}
// 0 is right 1 is left
int turn_at_intersection(int turn_direction)
{
 /*
  * This function is used to have the robot turn either left or right at an intersection (obviously your bot can not just
  * drive forward!). 
  * 
  * If turn_direction=0, turn right, else if turn_direction=1, turn left.
  * 
  * You're free to implement this in any way you like, but it should reliably leave your bot facing the correct direction
  * and on a street it can follow. 
  * 
  * You can use the return value to indicate success or failure, or to inform your code of the state of the bot
  */
  int target_angle = turn_direction == 1 ? -90 : 90;
  isRotating = 1;
  init_angle = get_angle();
  past_angle = get_angle();
  while (isRotating) {
    rotate_to(target_angle);
  }
  return(0);
}

int robot_localization(int *robot_x, int *robot_y, int *direction)
{
 /*  This function implements the main robot localization process. You have to write all code that will control the robot
  *  and get it to carry out the actions required to achieve localization.
  *
  *  Localization process:
  *
  *  - Find the street, and drive along the street toward an intersection
  *  - Scan the colours of buildings around the intersection
  *  - Update the beliefs in the beliefs[][] array according to the sensor measurements and the map data
  *  - Repeat the process until a single intersection/facing direction is distintly more likely than all the rest
  * 
  *  * We have provided headers for the following functions:
  * 
  *  find_street()
  *  drive_along_street()
  *  scan_intersection()
  *  turn_at_intersection()
  * 
  *  You *do not* have to use them, and can write your own to organize your robot's work as you like, they are
  *  provided as a suggestion.
  * 
  *  Note that *your bot must explore* the map to achieve reliable localization, this means your intersection
  *  scanning strategy should not rely exclusively on moving forward, but should include turning and exploring
  *  other streets than the one your bot was initially placed on.
  * 
  *  For each of the control functions, however, you will need to use the EV3 API, so be sure to become familiar with
  *  it.
  * 
  *  In terms of sensor management - the API allows you to read colours either as indexed values or RGB, it's up to
  *  you which one to use, and how to interpret the noisy, unreliable data you're likely to get from the sensor
  *  in order to update beliefs.
  * 
  *  HOWEVER: *** YOU must document clearly both in comments within this function, and in your report, how the
  *               sensor is used to read colour data, and how the beliefs are updated based on the sensor readings.
  * 
  *  DO NOT FORGET - Beliefs should always remain normalized to be a probability distribution, that means the
  *                  sum of beliefs over all intersections and facing directions must be 1 at all times.
  * 
  *  The function receives as input pointers to three integer values, these will be used to store the estimated
  *   robot's location and facing direction. The direction is specified as:
  *   0 - UP
  *   1 - RIGHT
  *   2 - BOTTOM
  *   3 - LEFT
  * 
  *  The function's return value is 1 if localization was successful, and 0 otherwise.
  */
 
  /************************************************************************************************************************
   *   TO DO  -   Complete this function
   ***********************************************************************************************************************/
   printBeliefs();
   printf("\n");
  while (!beliefsHasUnipueMax()) {
  
  
    printf("localization\n");
    int a[4];
    scan_intersection(&a[0], &a[1], &a[2], &a[3]);
    a[0] = change_color(a[0]);
    a[1] = change_color(a[1]);
    a[2] = change_color(a[2]);
    a[3] = change_color(a[3]);

    printf("%d %d %d %d\n", a[0], a[1], a[2], a[3]);

    updateBeliefByColor(&a[0], &a[1], &a[2], &a[3]);
    printBeliefs();
    printf("color\n");

    int red = drive_along_street();
    updateBeliefByAction(red);
    printBeliefs();
    printf("action\n");
  }

  int length = sx*sy;
  double max = 0;
  int max_x = 0, max_y = 0, max_dir = 0;

  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      if (beliefs[i][j] > max) {
        max = beliefs[i][j];
        max_x = i%sx;
        max_y = i/sx;
        max_dir = j;
      }
    }
  }
  *robot_x = max_x;
  *robot_y = max_y;
  *direction = max_dir;
  
  // printf("%d\n",beliefsHasUnipueMax());
  // int a[4];
  // scan_intersection(&a[0], &a[1], &a[2], &a[3]);
  // updateBeliefByColor(&a[0], &a[1], &a[2], &a[3]);
  // while(!beliefsHasUnipueMax()){
  //   int a[4];
  //   scan_intersection(&a[0], &a[1], &a[2], &a[3]);
  // }
 // Return an invalid location/direction and notify that localization was unsuccessful (you will delete this and replace it
 // with your code).

 
 return(0);
}
// perform an intersection scan and verify whether the colors scanned are the same as the colors on the map at the given robot location and direction
int verify_colors(int robot_x, int robot_y, int direction) {
  int colors[4];
  scan_intersection(&colors[0], &colors[1], &colors[2], &colors[3]);
  int index = get_index(robot_x, robot_y);

  colors[0] = change_color(colors[0]);
  colors[1] = change_color(colors[1]);
  colors[2] = change_color(colors[2]);
  colors[3] = change_color(colors[3]);

  printf("this is the color value at intersection %d %d %d %d\n", colors[0], colors[1], colors[2], colors[3]);
  printf("this is actual the color value at intersection %d %d %d %d\n", colors[0], colors[1], colors[2], colors[3]);


  if (direction == 0) {
    if (colors[0] != map[index][0] || colors[1] != map[index][1] || colors[2] != map[index][2] || colors[3] != map[index][3]) {
      return 1;
    }
    return 0;
  } else if (direction == 1) {
    if (colors[0] != map[index][1] || colors[1] != map[index][2] || colors[2] != map[index][3] || colors[3] != map[index][0]) {
      return 1;
    }
    return 0;
  } else if (direction == 2) {
    if (colors[0] != map[index][2] || colors[1] != map[index][3] || colors[2] != map[index][0] || colors[3] != map[index][1]) {
      
      return 1;
    }
    return 0;
  } else {
    if (colors[0] != map[index][3] || colors[1] != map[index][0] || colors[2] != map[index][1] || colors[3] != map[index][2]) {
      return 1;
    }
    return 0;
  }
}

int go_to_target(int robot_x, int robot_y, int direction, int target_x, int target_y)
{
 /*
  * This function is called once localization has been successful, it performs the actions required to take the robot
  * from its current location to the specified target location. 
  *
  * You have to write the code required to carry out this task - once again, you can use the function headers provided, or
  * write your own code to control the bot, but document your process carefully in the comments below so your TA can easily
  * understand how everything works.
  *
  * Your code should be able to determine if the robot has gotten lost (or if localization was incorrect), and your bot
  * should be able to recover.
  * 
  * Inputs - The robot's current location x,y (the intersection coordinates, not image pixel coordinates)
  *          The target's intersection location
  * 
  * Return values: 1 if successful (the bot reached its target destination), 0 otherwise
  */   

  /************************************************************************************************************************
   *   TO DO  -   Complete this function
   ***********************************************************************************************************************/
  
    if (robot_x > target_x) {
      if (direction == 0) {
        // rotate_to(-90);
        turn_at_intersection(1);
        direction = 3;
      } else if (direction == 1) {
        // rotate_to(190);
        turn_at_intersection(0);
        turn_at_intersection(0);
        direction = 3;
      } else if (direction == 2) {
        // rotate_to(90);
        turn_at_intersection(0);
        direction = 3;
      }
  } else if (robot_x < target_x) {
    if (direction == 0) {
      // rotate_to(90);
      turn_at_intersection(0);
      direction = 1;
    } else if (direction == 2) {
      // rotate_to(-90);
      turn_at_intersection(1);
      direction = 1;
    } else if (direction == 3) {
      // rotate_to(190);
      turn_at_intersection(0);
      turn_at_intersection(0);
      direction = 1;
    }
  }
  // if (!verify_colors(robot_x, robot_y, direction)) {
  //   return 0;
  // }

  while (robot_x != target_x) {
    drive_along_street();
    if (robot_x > target_x) {
      robot_x--;
    } else {
      robot_x++;
    }
    // if (!verify_colors(robot_x, robot_y, direction)) {
    //   return 0;
    // }
  }

  if (robot_y == target_y) {
    return 1;
  }

  if (robot_y > target_y) {
    if (direction == 2) {
      // rotate_to(190);
      turn_at_intersection(0);
      turn_at_intersection(0);
      direction = 0;
    } else if (direction == 1) {
      // rotate_to(90);
      turn_at_intersection(1);
      direction = 0;
    } else if (direction == 3) {
      // rotate_to(-90);
      turn_at_intersection(0);
      direction = 0;
    }
  } else if (robot_y < target_y) {
    if (direction == 0) {
      // rotate_to(-90);
      turn_at_intersection(0);
      turn_at_intersection(0);
      direction = 2;
    } else if (direction == 1) {
      // rotate_to(190);
      turn_at_intersection(0);
      direction = 2;
    } else if (direction == 3) {
      // rotate_to(90);
      turn_at_intersection(1);
      direction = 2;
    }
  }
  // if (!verify_colors(robot_x, robot_y, direction)) {
  //   return 0;
  // }

  while (robot_y != target_y) {
    drive_along_street();
    if (robot_y > target_y) {
      robot_y--;
    } else {
      robot_y++;
    }
    // if (!verify_colors(robot_x, robot_y, direction)) {
    //   return 0;
    // }
  }

  return 1;
  }

// compute the distance between two colors
// reference https://www.compuphase.com/cmetric.htm
double color_distance(int* rgba, int* rgbb) {
  long rmean = ((long)rgba[0] + (long)rgbb[0])/2;
  long r = (long)rgba[0]-(long)rgbb[0];
  long g = (long)rgba[1]-(long)rgbb[1];
  long b = (long)rgba[2]-(long)rgbb[2];
  return sqrt((((512+rmean)*r*r)>>8) + 4*g*g + (((767-rmean)*b*b)>>8));
}

// Rotate to angle
void rotate_to(int angle) {
  int cur_angle = get_angle();
  printf("init: %d, cur: %d\n", init_angle, cur_angle);
  if(abs(cur_angle-past_angle)>20) {
    init_angle += cur_angle-past_angle;
  }
  if(abs(cur_angle-init_angle-angle)<3){
    isRotating = 0;
    BT_all_stop(0);
  }else if(cur_angle-init_angle>angle){
    if(angle>100){
      BT_turn(MOTOR_A, -10, MOTOR_D, 10);
    }else{
      BT_turn(MOTOR_A, -10, MOTOR_D, 9);
    }
  }else if(cur_angle-init_angle<=angle){
    BT_turn(MOTOR_A, 10, MOTOR_D, -8);
  }
  past_angle = cur_angle;
  return;
}
// get current angle
int get_angle() {
  int angle = BT_read_gyro_sensor(PORT_3);
  angle = angle%360;
  if(angle<0){
    angle += 360;
  }
  return angle;
}
// center the color sensor
void center_sensor(){
  for (int i = 0; i < 300; i++)
  {
    BT_motor_port_start(MOTOR_C, -5);
  }
  BT_all_stop(1);
  for (int i = 0; i < 68; i++)
  {
    BT_motor_port_start(MOTOR_C, 5);
  }
  BT_all_stop(1);
}
void calibrate_sensor(void)
{
 /*
  * This function is called when the program is started with -1  -1 for the target location. 
  *
  * You DO NOT NEED TO IMPLEMENT ANYTHING HERE - but it is strongly recommended as good calibration will make sensor
  * readings more reliable and will make your code more resistent to changes in illumination, map quality, or battery
  * level.
  * 
  * The principle is - Your code should allow you to sample the different colours in the map, and store representative
  * values that will help you figure out what colours the sensor is reading given the current conditions.
  * 
  * Inputs - None
  * Return values - None - your code has to save the calibration information to a file, for later use (see in main())
  * 
  * How to do this part is up to you, but feel free to talk with your TA and instructor about it!
  */   
//  if any value is over 255, take 255
  // GREEN: (60, 170, 80)
  // RED: (255, 60, 60)
  // BLUE: (3O, 70, 130)
  // BLACK: (35, 45, 40)
  // YELLOW: (255, 255, 95)

  /************************************************************************************************************************
   *   OIPTIONAL TO DO  -   Complete this function
   ***********************************************************************************************************************/
//   if (BT_open(HEXKEY)!=0)
//   {
//     fprintf(stderr,"Unable to open comm socket to the EV3, make sure the EV3 kit is powered on, and that the\n");
//     fprintf(stderr," hex key for the EV3 matches the one in EV3_Localization.h\n");
//     exit(1);
//   }

//  fprintf(stderr,"All set, ready to go!\n");
//   int green[3] = {60, 170, 80};
//   int red[3] = {255, 60, 60};
//   int blue[3] = {30, 70, 130};
//   int black[3] = {35, 45, 40};
//   int yellow[3] = {255, 255, 95};
//   int white[3] = {0, 0, 0};
//   int rgb[3];
//   int initial_angle = get_angle();
  
//   while (1) {
//     printf("%d\n", get_angle());
//     // BT_turn(MOTOR_A, 10, MOTOR_D, -10);
//   }
//   return;

//   BT_read_colour_sensor_RGB(PORT_2, rgb);
//   double min_dist = fmin(fmin(fmin(color_distance(rgb, green), color_distance(rgb, red)), fmin(color_distance(rgb, blue), color_distance(rgb, black))), fmin(color_distance(rgb, yellow), color_distance(rgb, white)));
//   int r = 0, g = 0, b = 0, k = 0, y = 0, w = 0;
//   while (!(r || g || b|| k || y || w)) {
//     while (color_distance(rgb, red) == min_dist) {
//       red[0] = rgb[0];
//       red[1] = rgb[1];
//       red[2] = rgb[2];

//       r = 1;
//       while (BT_read_gyro_sensor(PORT_3) < 90) {
//         BT_turn(MOTOR_A, 10, MOTOR_D, -10);
//       }
//       BT_read_colour_sensor_RGB(PORT_2, rgb);
//     }

//     BT_read_colour_sensor_RGB(PORT_2, rgb);
//     double min_dist = fmin(fmin(fmin(color_distance(rgb, green), color_distance(rgb, red)), fmin(color_distance(rgb, blue), color_distance(rgb, black))), fmin(color_distance(rgb, yellow), color_distance(rgb, white)));

//     if (color_distance(rgb, green) == min_dist) {
//       green[0] = rgb[0];
//       green[1] = rgb[1];
//       green[2] = rgb[2];
//       g = 1;
//     } else if (color_distance(rgb, blue) == min_dist) {
//       blue[0] = rgb[0];
//       blue[1] = rgb[1];
//       blue[2] = rgb[2];
//       b = 1;
//     } else if (color_distance(rgb, black) == min_dist) {
//       black[0] = rgb[0];
//       black[1] = rgb[1];
//       black[2] = rgb[2];
//       k = 1;
//     } else if (color_distance(rgb, yellow) == min_dist) {
//       yellow[0] = rgb[0];
//       yellow[1] = rgb[1];
//       yellow[2] = rgb[2];
//       y = 1;
//     } else {
//       white[0] = rgb[0];
//       white[1] = rgb[1];
//       white[2] = rgb[2];
//       w = 1;
//     }
//     BT_drive(MOTOR_A, MOTOR_D, 10);
//   }
//   FILE *fptr;
//   fptr = fopen("calibrated_colors.txt", "w");
//   fprintf(fptr, "%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n%d,%d,%d\n", red[0], red[1], red[2], green[0], green[1], green[2], blue[0], blue[1], blue[2], black[0], black[1], black[2], yellow[0], yellow[1], yellow[2], white[0], white[1], white[2]);
//   fclose(fptr);
//   BT_close();
  fprintf(stderr,"Calibration function called!\n");
  return;
}
//convert char color values to int color values
int change_color(char c) {
  if (c == 'k') {
    return 1;
  } else if (c == 'b') {
    return 2;
  } else if (c == 'y') {
    return 4;
  } else if (c == 'g') {
    return 3;
  } else if (c == 'r') {
    return 5;
  } else {
    return 6;
  }
}
// returns the char associated with the color given in rgb
char what_color(int* rgb) {
  int green[3] = {60, 170, 80};
  int red[3] = {255, 60, 60};
  int blue[3] = {30, 70, 130};
  int black[3] = {35, 45, 40};
  int yellow[3] = {255, 255, 95};
  int white[3] = {200, 230, 255};

  double min_dist = fmin(fmin(fmin(color_distance(rgb, green), color_distance(rgb, red)), fmin(color_distance(rgb, blue), color_distance(rgb, black))), fmin(color_distance(rgb, yellow), color_distance(rgb, white)));
  if (color_distance(rgb, red) == min_dist) {
    return 'r';
  } else if (color_distance(rgb, green) == min_dist) {
    return 'g';
  } else if (color_distance(rgb, blue) == min_dist) {
    return 'b';
  } else if (color_distance(rgb, black) == min_dist) {
    return 'k';
  } else if (color_distance(rgb, yellow) == min_dist) {
    return 'y';
  } else {
    return 'w';
  }
}
// get index for map array given x and y location
int get_index(int x, int y){
  return x*sx+y;
}
// print the beliefs array
void printBeliefs(){
  int length = sx*sy;
  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      printf("%f ", beliefs[i][j]);
    }
    printf("\n ");
  }
}
// normalize the beliefs array
void normalizeBeliefs(){
  int length = sx*sy;
  //sum up
  double sum = 0;
  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      sum += beliefs[i][j];
    }
  }
  //divide
  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      beliefs[i][j] = beliefs[i][j]/sum;
    }
  }
}
// return whether belief array has a unique max or not
int beliefsHasUnipueMax(){
  int length = sx*sy;
  double max = 0;
  //find max
  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      if(beliefs[i][j] > max){
        max = beliefs[i][j];
      }
    }
  }

  //find unipue
  int count = 0;
  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      if(beliefs[i][j] == max){
        count += 1;
      }
    }
  }
  
  return count == 1;
}

void updateBeliefByColor(int *tl, int *tr, int *br, int *bl){
  int length = sx*sy;
  int direction;
  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      direction = color_match(tl, tr, br, bl, &map[i][0], &map[i][1], &map[i][2], &map[i][3]);
      if(direction >= 0){
        beliefs[i][direction] = 9*beliefs[i][direction];
      }
    }
  }
  normalizeBeliefs();
}

void updateBeliefByAction(int touchRed){
  int length = sx*sy;
  double p = 0;
  double beliefsCopy[400][4];
  double small = 0.000001;
  for (int i = 0; i < length; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      beliefsCopy[i][j] = beliefs[i][j];
    }
  }
  if(touchRed){
    for (int i = 0; i < length; i++)
    {
        if (i%sx == 0 && i/sx == 0) {//left top
          beliefs[i][0] = beliefsCopy[i][0]*small;
          beliefs[i][1] = beliefsCopy[i][3];
          beliefs[i][2] = beliefsCopy[i][0];
          beliefs[i][3] = beliefsCopy[i][3]*small;
        } else if (i%sx == sx-1 && i/sx == 0) {//right top
          beliefs[i][0] = beliefsCopy[i][0]*small;
          beliefs[i][1] = beliefsCopy[i][1]*small;
          beliefs[i][2] = beliefsCopy[i][0];
          beliefs[i][3] = beliefsCopy[i][1];
        }else if (i/sx == 0){//top mid
          beliefs[i][0] = beliefsCopy[i][0]*small;
          beliefs[i][1] = beliefsCopy[i][1]*small;
          beliefs[i][2] = beliefsCopy[i][0];
          beliefs[i][3] = beliefsCopy[i][3]*small;
        }else if(i/sx == sy-1 && i%sx == 0){//bottom left
          beliefs[i][0] = beliefsCopy[i][2];
          beliefs[i][1] = beliefsCopy[i][3];
          beliefs[i][2] = beliefsCopy[i][0]*small;
          beliefs[i][3] = beliefsCopy[i][3]*small;
        }else if (i/sx == sy-1 && i%sx == sx-1){//bottom right
          beliefs[i][0] = beliefsCopy[i][2];
          beliefs[i][1] = beliefsCopy[i][1]*small;
          beliefs[i][2] = beliefsCopy[i][0]*small;
          beliefs[i][3] = beliefsCopy[i][1];
        } else if (i/sx == sy-1) {//bottom mid
          beliefs[i][0] = beliefsCopy[i][2];
          beliefs[i][1] = beliefsCopy[i][1]*small;
          beliefs[i][2] = beliefsCopy[i][0]*small;
          beliefs[i][3] = beliefsCopy[i][3]*small;
        } else if (i%sx == 0) {//left mid
          beliefs[i][0] = beliefsCopy[i][0]*small;
          beliefs[i][1] = beliefsCopy[i][3];
          beliefs[i][2] = beliefsCopy[i][0]*small;
          beliefs[i][3] = beliefsCopy[i][3]*small;
        } else if (i%sx == sx-1) {
          beliefs[i][0] = beliefsCopy[i][0]*small;
          beliefs[i][1] = beliefsCopy[i][0]*small;
          beliefs[i][2] = beliefsCopy[i][0]*small;
          beliefs[i][3] = beliefsCopy[i][1];
        } else {
          beliefs[i][0] = beliefsCopy[i][0]*small;
          beliefs[i][1] = beliefsCopy[i][0]*small;
          beliefs[i][2] = beliefsCopy[i][0]*small;
          beliefs[i][3] = beliefsCopy[i][3]*small;
        }
    }
  }else{
    for (int i = 0; i < length; i++)
    {
      if (i%sx == 0 && i/sx == 0) {//left top
        beliefs[i][0] = beliefsCopy[i+sx][0];
        beliefs[i][1] = beliefsCopy[i][1]*small;
        beliefs[i][2] = beliefsCopy[i][2]*small;
        beliefs[i][3] = beliefsCopy[i+1][3];
      } else if (i%sx == sx-1 && i/sx == 0) {//right top
        beliefs[i][0] = beliefsCopy[i+sx][0];
        beliefs[i][1] = beliefsCopy[i-1][1];
        beliefs[i][2] = beliefsCopy[i][2]*small;
        beliefs[i][3] = beliefsCopy[i][3]*small;
      }else if (i/sx == 0){//top mid
        beliefs[i][0] = beliefsCopy[i+sx][0];
        beliefs[i][1] = beliefsCopy[i-1][1];
        beliefs[i][2] = beliefsCopy[i][2]*small;
        beliefs[i][3] = beliefsCopy[i+1][3];
      }else if(i/sx == sy-1 && i%sx == 0){//bottom left
        beliefs[i][0] = beliefsCopy[i][0]*small;
        beliefs[i][1] = beliefsCopy[i][1]*small;
        beliefs[i][2] = beliefsCopy[i-sx][2];
        beliefs[i][3] = beliefsCopy[i+1][3];
      }else if (i/sx == sy-1 && i%sx == sx-1){//bottom right
        beliefs[i][0] = beliefsCopy[i][0]*small;
        beliefs[i][1] = beliefsCopy[i-1][1];
        beliefs[i][2] = beliefsCopy[i-sx][2];
        beliefs[i][3] = beliefsCopy[i][3]*small;
      } else if (i/sx == sy-1) {//bottom mid
        beliefs[i][0] = beliefsCopy[i][0]*small;
        beliefs[i][1] = beliefsCopy[i-1][1];
        beliefs[i][2] = beliefsCopy[i-sx][2];
        beliefs[i][3] = beliefsCopy[i+1][3];
      } else if (i%sx == 0) {//left mid
        beliefs[i][0] = beliefsCopy[i+sx][0];
        beliefs[i][1] = beliefsCopy[i][1]*small;
        beliefs[i][2] = beliefsCopy[i-sx][2];
        beliefs[i][3] = beliefsCopy[i+1][3];
      } else if (i%sx == sx-1) {
        beliefs[i][0] = beliefsCopy[i+sx][0];
        beliefs[i][1] = beliefsCopy[i-1][1];
        beliefs[i][2] = beliefsCopy[i-sx][2];
        beliefs[i][3] = beliefsCopy[i][3]*small;
      } else {
        beliefs[i][0] = beliefsCopy[i+sx][0];
        beliefs[i][1] = beliefsCopy[i-1][1];
        beliefs[i][2] = beliefsCopy[i-sx][2];
        beliefs[i][3] = beliefsCopy[i+1][3];
      }
    }
  }
  normalizeBeliefs();
}

int color_match(int *tl1, int *tr1, int *br1, int *bl1, int *tl2, int *tr2, int *br2, int *bl2){
  //return direction if color match in any direction, -1 if no match
  if(*(tl1)==*(tl2) && *(tr1)==*(tr2) && *(br1)==*(br2) && *(bl1)==*(bl2)){
    return 0;
  }
  else if(*(tl1)==*(tr2) && *(tr1)==*(br2) && *(br1)==*(bl2) && *(bl1)==*(tl2)){
    return 1;
  }else if(*(tl1)==*(br2) && *(tr1)==*(bl2) && *(br1)==*(tl2) && *(bl1)==*(tr2)){
    return 2;
  }else if (*(tl1)==*(bl2) && *(tr1)==*(tl2) && *(br1)==*(tr2) && *(bl1)==*(br2))
  {
    return 3;
  }
  return -1;
}

int parse_map(unsigned char *map_img, int rx, int ry)
{
 /*
   This function takes an input image map array, and two integers that specify the image size.
   It attempts to parse this image into a representation of the map in the image. The size
   and resolution of the map image should not affect the parsing (i.e. you can make your own
   maps without worrying about the exact position of intersections, roads, buildings, etc.).

   However, this function requires:
   
   * White background for the image  [255 255 255]
   * Red borders around the map  [255 0 0]
   * Black roads  [0 0 0]
   * Yellow intersections  [255 255 0]
   * Buildings that are pure green [0 255 0], pure blue [0 0 255], or white [255 255 255]
   (any other colour values are ignored - so you can add markings if you like, those 
    will not affect parsing)

   The image must be a properly formated .ppm image, see readPPMimage below for details of
   the format. The GIMP image editor saves properly formatted .ppm images, as does the
   imagemagick image processing suite.
   
   The map representation is read into the map array, with each row in the array corrsponding
   to one intersection, in raster order, that is, for a map with k intersections along its width:
   
    (row index for the intersection)
    
    0     1     2    3 ......   k-1
    
    k    k+1   k+2  ........    
    
    Each row will then contain the colour values for buildings around the intersection 
    clockwise from top-left, that is
    
    
    top-left               top-right
            
            intersection
    
    bottom-left           bottom-right
    
    So, for the first intersection (at row 0 in the map array)
    map[0][0] <---- colour for the top-left building
    map[0][1] <---- colour for the top-right building
    map[0][2] <---- colour for the bottom-right building
    map[0][3] <---- colour for the bottom-left building
    
    Color values for map locations are defined as follows (this agrees with what the
    EV3 sensor returns in indexed-colour-reading mode):
    
    1 -  Black
    2 -  Blue
    3 -  Green
    4 -  Yellow
    5 -  Red
    6 -  White
    
    If you find a 0, that means you're trying to access an intersection that is not on the
    map! Also note that in practice, because of how the map is defined, you should find
    only Green, Blue, or White around a given intersection.
    
    The map size (the number of intersections along the horizontal and vertical directions) is
    updated and left in the global variables sx and sy.

    Feel free to create your own maps for testing (you'll have to print them to a reasonable
    size to use with your bot).
    
 */    
 
 int last3[3];
 int x,y;
 unsigned char R,G,B;
 int ix,iy;
 int bx,by,dx,dy,wx,wy;         // Intersection geometry parameters
 int tgl;
 int idx;
 
 ix=iy=0;       // Index to identify the current intersection
 
 // Determine the spacing and size of intersections in the map
 tgl=0;
 for (int i=0; i<rx; i++)
 {
  for (int j=0; j<ry; j++)
  {
   R=*(map_img+((i+(j*rx))*3));
   G=*(map_img+((i+(j*rx))*3)+1);
   B=*(map_img+((i+(j*rx))*3)+2);
   if (R==255&&G==255&&B==0)
   {
    // First intersection, top-left pixel. Scan right to find width and spacing
    bx=i;           // Anchor for intersection locations
    by=j;
    for (int k=i; k<rx; k++)        // Find width and horizontal distance to next intersection
    {
     R=*(map_img+((k+(by*rx))*3));
     G=*(map_img+((k+(by*rx))*3)+1);
     B=*(map_img+((k+(by*rx))*3)+2);
     if (tgl==0&&(R!=255||G!=255||B!=0))
     {
      tgl=1;
      wx=k-i;
     }
     if (tgl==1&&R==255&&G==255&&B==0)
     {
      tgl=2;
      dx=k-i;
     }
    }
    for (int k=j; k<ry; k++)        // Find height and vertical distance to next intersection
    {
     R=*(map_img+((bx+(k*rx))*3));
     G=*(map_img+((bx+(k*rx))*3)+1);
     B=*(map_img+((bx+(k*rx))*3)+2);
     if (tgl==2&&(R!=255||G!=255||B!=0))
     {
      tgl=3;
      wy=k-j;
     }
     if (tgl==3&&R==255&&G==255&&B==0)
     {
      tgl=4;
      dy=k-j;
     }
    }
    
    if (tgl!=4)
    {
     fprintf(stderr,"Unable to determine intersection geometry!\n");
     return(0);
    }
    else break;
   }
  }
  if (tgl==4) break;
 }
  fprintf(stderr,"Intersection parameters: base_x=%d, base_y=%d, width=%d, height=%d, horiz_distance=%d, vertical_distance=%d\n",bx,by,wx,wy,dx,dy);

  sx=0;
  for (int i=bx+(wx/2);i<rx;i+=dx)
  {
   R=*(map_img+((i+(by*rx))*3));
   G=*(map_img+((i+(by*rx))*3)+1);
   B=*(map_img+((i+(by*rx))*3)+2);
   if (R==255&&G==255&&B==0) sx++;
  }

  sy=0;
  for (int j=by+(wy/2);j<ry;j+=dy)
  {
   R=*(map_img+((bx+(j*rx))*3));
   G=*(map_img+((bx+(j*rx))*3)+1);
   B=*(map_img+((bx+(j*rx))*3)+2);
   if (R==255&&G==255&&B==0) sy++;
  }
  
  fprintf(stderr,"Map size: Number of horizontal intersections=%d, number of vertical intersections=%d\n",sx,sy);

  // Scan for building colours around each intersection
  idx=0;
  for (int j=0; j<sy; j++)
   for (int i=0; i<sx; i++)
   {
    x=bx+(i*dx)+(wx/2);
    y=by+(j*dy)+(wy/2);
    
    fprintf(stderr,"Intersection location: %d, %d\n",x,y);
    // Top-left
    x-=wx;
    y-=wy;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][0]=3;
    else if (R==0&&G==0&&B==255) map[idx][0]=2;
    else if (R==255&&G==255&&B==255) map[idx][0]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Top-Left RGB=%d,%d,%d\n",i,j,R,G,B);

    // Top-right
    x+=2*wx;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][1]=3;
    else if (R==0&&G==0&&B==255) map[idx][1]=2;
    else if (R==255&&G==255&&B==255) map[idx][1]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Top-Right RGB=%d,%d,%d\n",i,j,R,G,B);

    // Bottom-right
    y+=2*wy;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][2]=3;
    else if (R==0&&G==0&&B==255) map[idx][2]=2;
    else if (R==255&&G==255&&B==255) map[idx][2]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Bottom-Right RGB=%d,%d,%d\n",i,j,R,G,B);
    
    // Bottom-left
    x-=2*wx;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][3]=3;
    else if (R==0&&G==0&&B==255) map[idx][3]=2;
    else if (R==255&&G==255&&B==255) map[idx][3]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Bottom-Left RGB=%d,%d,%d\n",i,j,R,G,B);
    
    fprintf(stderr,"Colours for this intersection: %d, %d, %d, %d\n",map[idx][0],map[idx][1],map[idx][2],map[idx][3]);
    
    idx++;
   }

 return(1);  
}

unsigned char *readPPMimage(const char *filename, int *rx, int *ry)
{
 // Reads an image from a .ppm file. A .ppm file is a very simple image representation
 // format with a text header followed by the binary RGB data at 24bits per pixel.
 // The header has the following form:
 //
 // P6
 // # One or more comment lines preceded by '#'
 // 340 200
 // 255
 //
 // The first line 'P6' is the .ppm format identifier, this is followed by one or more
 // lines with comments, typically used to inidicate which program generated the
 // .ppm file.
 // After the comments, a line with two integer values specifies the image resolution
 // as number of pixels in x and number of pixels in y.
 // The final line of the header stores the maximum value for pixels in the image,
 // usually 255.
 // After this last header line, binary data stores the RGB values for each pixel
 // in row-major order. Each pixel requires 3 bytes ordered R, G, and B.
 //
 // NOTE: Windows file handling is rather crotchetty. You may have to change the
 //       way this file is accessed if the images are being corrupted on read
 //       on Windows.
 //

 FILE *f;
 unsigned char *im;
 char line[1024];
 int i;
 unsigned char *tmp;
 double *fRGB;

 im=NULL;
 f=fopen(filename,"rb+");
 if (f==NULL)
 {
  fprintf(stderr,"Unable to open file %s for reading, please check name and path\n",filename);
  return(NULL);
 }
 fgets(&line[0],1000,f);
 if (strcmp(&line[0],"P6\n")!=0)
 {
  fprintf(stderr,"Wrong file format, not a .ppm file or header end-of-line characters missing\n");
  fclose(f);
  return(NULL);
 }
 fprintf(stderr,"%s\n",line);
 // Skip over comments
 fgets(&line[0],511,f);
 while (line[0]=='#')
 {
  fprintf(stderr,"%s",line);
  fgets(&line[0],511,f);
 }
 sscanf(&line[0],"%d %d\n",rx,ry);                  // Read image size
 fprintf(stderr,"nx=%d, ny=%d\n\n",*rx,*ry);

 fgets(&line[0],9,f);  	                // Read the remaining header line
 fprintf(stderr,"%s\n",line);
 im=(unsigned char *)calloc((*rx)*(*ry)*3,sizeof(unsigned char));
 if (im==NULL)
 {
  fprintf(stderr,"Out of memory allocating space for image\n");
  fclose(f);
  return(NULL);
 }
 fread(im,(*rx)*(*ry)*3*sizeof(unsigned char),1,f);
 fclose(f);

 return(im);    
}