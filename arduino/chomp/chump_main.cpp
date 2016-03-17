#include "Arduino.h"
#include "chump_main.h"
#include "rc.h"
#include "leddar_io.h"
#include "autofire.h"
#include "chump_targeting.h"
#include "sensors.h"
#include "xbee.h"
#include "drive.h"
#include "telem.h"
#include "pins.h"
#include "SoftwareSerial.h"


void printMiddleDistance(unsigned int num_detections, Detection* detections) {
    int segment7_dist = 10000;
    int segment8_dist = 10000;
    for (uint8_t i =0; i < num_detections; i++) {
        if (detections[i].Segment == 7 && detections[i].Distance < segment7_dist) {
            segment7_dist = detections[i].Distance;
        } else if (detections[i].Segment == 8 && detections[i].Distance < segment8_dist) {
            segment8_dist = detections[i].Distance;
        }
    }
    if (segment7_dist == 10000 && segment8_dist == 10000) {
        Debug.print("n/a");
        Debug.print("\t");
    } else if (segment7_dist == 10000) {
        Debug.print(segment8_dist);
        Debug.print("\t");
    } else if (segment8_dist == 10000) {
        Debug.print(segment7_dist);
        Debug.print("\t");
    } else {
        Debug.print((segment7_dist + segment8_dist) / 2);
        Debug.print("\t");
    }
}


void chumpSetup() {
  leddarWrapperInit();
  attachRCInterrupts();
  requestDetections();
  Debug.begin(115200);
  Sbus.begin(100000);
  Sbus.setTimeout(10);
  LeftWheelSerial.begin(115200);
  RightWheelSerial.begin(115200);
}

static char previous_rc_bitfield = 0;
static int16_t left_drive_value = 0;
static int16_t right_drive_value = 0;
bool targeting_enable = getTargetingEnable() > 0.09;
static unsigned long last_request_time = micros();
static int16_t steer_bias = 0; // positive turns right, negative turns left
// Object Nearest_obj;
// static float target_angle = 0.0;

// new loop for chump driving should get pwm values set by interrupts to send out pwm to output pins, which will go to motor controllers. 
// setup function needs to put these at appropriate neutral values. need some global enable too to make sure that radio contact 
// is ensured! set another pin to self drive mode, and that mode can do something like spin left for 1s, then right for 1s, and so on 
// for demonstration
unsigned long last_loop_begin = micros();

void chumpLoop() {
//  Serial.println(micros() - last_loop_begin);
//  last_loop_begin = micros();

  unsigned long start_time = micros();
  if (micros() - last_request_time > 1000000) {
    last_request_time = micros();
    requestDetections();
  }
  
  // check if there is new Leddar data
  bool complete = bufferDetections();
  if (complete) {
    unsigned int detection_count = parseDetections();
    last_request_time = micros();
    int current_leddar_state = getState(detection_count, getDetections());
    requestDetections();
    // steer_bias = pidSteer(detection_count, getDetections());
    if (targeting_enable && left_drive_value <= 600) {
        left_drive_value += 50;
        right_drive_value += 50;
        printMiddleDistance(detection_count, getDetections());
        Debug.print(left_drive_value);
        Debug.print("\t");
        Debug.print(right_drive_value);
        Debug.println();
    }
  }

  // should this be renamed to weapons_rc for clarity?
  bool rc_complete = bufferSbusData();
  if (rc_complete) {
    parseSbus();
  }

//   left_drive_value = getLeftRc();
//   right_drive_value = getRightRc();

  // right side value needs to be reversed for chump because of motor configuration. might differ on chomp.
  if (!targeting_enable) {
    // left_drive_value = 0;
    // right_drive_value = 0;
    left_drive_value = getLeftRc();
    right_drive_value = getRightRc();
  }

  targeting_enable = getTargetingEnable() > 0.09;
//   if (targetingEnable) {
//     left_drive_value += steer_bias;
//     right_drive_value += steer_bias;
//     Debug.print(steer_bias);
//     Debug.println();
//   }

  float l_tread_mix = left_drive_value;
  float r_tread_mix = -right_drive_value;

  driveL(l_tread_mix);
  driveR(r_tread_mix);
}
