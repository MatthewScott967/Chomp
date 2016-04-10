#include "Arduino.h"
#include "weapons.h"
#include "rc.h"
#include "sensors.h"
#include "pins.h"
#include "utils.h"


#define RELATIVE_TO_FORWARD 221  // offset of axle anfle from 180 when hammer forward on floor. actual angle read 221
#define RELATIVE_TO_VERTICAL 32  // offset of axle angle from 90 when hammer arms vertical. actual angle read 122
#define RELATIVE_TO_BACK 25  // offset of axle angle from 0 when hammer back on floor. actual angle read 25

bool weaponsEnabled(){
    return g_enabled;
}

bool autofireEnabled(char bitfield){
    return bitfield & AUTO_HAMMER_ENABLE_BIT;
}

// RETRACT CONSTANTS
static const uint32_t RETRACT_TIMEOUT = 4000 * 1000L;  // in microseconds
#define RETRACT_BEGIN_VEL_MAX 5
static const uint16_t RETRACT_COMPLETE_ANGLE = 53 + RELATIVE_TO_BACK;  // angle read  angle 53 off ground good on 4-09

// HAMMER DATA BUFFERS
static const uint16_t MAX_DATAPOINTS = 500;
static uint16_t angle_data[MAX_DATAPOINTS];
static int16_t pressure_data[MAX_DATAPOINTS];

// HAMMER THROW CONSTANTS
static const uint32_t SWING_TIMEOUT = 1000 * 1000L;  // in microseconds
static const uint32_t DATA_COLLECT_TIMESTEP = 2000L;  // timestep for data logging, in microseconds
static const uint16_t THROW_BEGIN_ANGLE_MIN = RELATIVE_TO_BACK - 5;
static const uint16_t THROW_BEGIN_ANGLE_MAX = RELATIVE_TO_BACK + 10;
#define THROW_CLOSE_ANGLE_DIFF 3  // angle distance between throw open and throw close
static const uint16_t VENT_OPEN_ANGLE = 175;
static const uint16_t THROW_COMPLETE_ANGLE = RELATIVE_TO_FORWARD;
#define THROW_COMPLETE_VELOCITY 0

void retract(){
    uint16_t start_angle;
    uint32_t sensor_read_time;
    uint32_t delay_time;
    bool angle_read_ok = readAngle(&start_angle);
    // if angle data isn't coming back, abort
    while (!angle_read_ok) {
        uint8_t start_attempts = 1;
        if (start_attempts < 5) {
            angle_read_ok = readAngle(&start_angle);
            start_attempts++;
        } else {
            return;
        }
    }
    uint16_t angle = start_angle;
    int16_t angular_velocity = 0;
    safeDigitalWrite(VENT_VALVE_DO, LOW);
    uint32_t retract_time;
    uint32_t angle_complete_time = 0;
    int16_t angle_traversed = 0;
    // Consider inferring hammer velocity here and requiring that it be below some threshold
    // Only retract if hammer is forward
    bool velocity_read_ok = angularVelocity(&angular_velocity);
    if (weaponsEnabled() && angle > RETRACT_COMPLETE_ANGLE && abs(angular_velocity) < RETRACT_BEGIN_VEL_MAX) {
    // if (weaponsEnabled()) {
        retract_time = micros();
        while (micros() - retract_time < RETRACT_TIMEOUT && angle > RETRACT_COMPLETE_ANGLE) {
        // while (micros() - retract_time < RETRACT_TIMEOUT) {
            sensor_read_time = micros();
            angle_read_ok = readAngle(&angle);
            DriveSerial.println("@05!G 100");  // start motor to aid meshing
            safeDigitalWrite(RETRACT_VALVE_DO, HIGH);
            DriveSerial.println("@05!G 1000");
            // Ensure that loop step takes 1 ms or more (without this it takes quite a bit less)
            sensor_read_time = micros() - sensor_read_time;
            delay_time = 10000 - sensor_read_time;
            if (delay_time > 0) {
                delayMicroseconds(delay_time);
            }
        }
        safeDigitalWrite(RETRACT_VALVE_DO, LOW);
        DriveSerial.println("@05!G 0");
        retract_time = micros() - retract_time;
    }
}

void fire(){
    uint32_t fire_time;
    uint32_t swing_length = 0;
    uint32_t sensor_read_time;
    uint16_t throw_close_timestep = 0;
    uint16_t vent_open_timestep = 0;
    uint16_t datapoints_collected = 0;
    uint16_t timestep = 0;
    bool vent_closed = false;
    bool throw_open = false;
    uint32_t delay_time;
    uint16_t angle;
    uint16_t start_angle;
    bool angle_read_ok;
    int16_t pressure;
    bool pressure_read_ok;
    int16_t angular_velocity;
    bool velocity_read_ok;
    
    if (weaponsEnabled()){
        bool angle_read_ok = readAngle(&angle);
        // if angle data isn't coming back, abort
        while (!angle_read_ok) {
            uint8_t start_attempts = 1;
            if (start_attempts < 5) {
                angle_read_ok = readAngle(&angle);
                start_attempts++;
            } else {
                return;
            }
        }
        start_angle = angle;
        uint16_t throw_close_angle = start_angle + THROW_CLOSE_ANGLE_DIFF;
        if (angle > THROW_BEGIN_ANGLE_MIN && angle < THROW_BEGIN_ANGLE_MAX) {
            // Debug.write("Fire!\r\n");
            // Seal vent (which is normally open)
            magOn();
            safeDigitalWrite(VENT_VALVE_DO, HIGH);
            Debug.println("vent close");
            vent_closed = true;
            // can we actually determine vent close time?
            delay(10);
            
            // Open throw valve
            safeDigitalWrite(THROW_VALVE_DO, HIGH);
            Debug.println("throw open");
            throw_open = true;
            fire_time = micros();
            // In fighting form, should probably just turn on flamethrower here
            // Wait until hammer swing complete, up to 1 second
            while (swing_length < SWING_TIMEOUT && angle < THROW_COMPLETE_ANGLE) {
                sensor_read_time = micros();
                angle_read_ok = readAngle(&angle);
                Debug.println(angle);
                pressure_read_ok = readMlhPressure(&pressure);
                // Keep throw valve open until 5 degrees
                if (throw_open && angle > throw_close_angle) {
                    throw_close_timestep = timestep;
                    safeDigitalWrite(THROW_VALVE_DO, LOW);
                    Debug.println("throw close");
                    throw_open = false;
                }
                if (vent_closed && angle > VENT_OPEN_ANGLE) {
                    vent_open_timestep = timestep;
                    safeDigitalWrite(VENT_VALVE_DO, LOW);
                    Debug.println("vent open");
                    vent_closed = false;
                }
                // close throw, open vent if hammer velocity below threshold after THROW_CLOSE_ANGLE_DIFF degrees traversed
                if (angle > (start_angle + THROW_CLOSE_ANGLE_DIFF)) {
                    velocity_read_ok = angularVelocityBuffered(&angular_velocity, angle_data, datapoints_collected);
                    if (velocity_read_ok && abs(angular_velocity) < THROW_COMPLETE_VELOCITY) {
                        if (throw_open) {throw_close_timestep = timestep;}
                        safeDigitalWrite(THROW_VALVE_DO, LOW);
                        Debug.println("throw close");
                        throw_open = false;
                        delay(10);
                        if (vent_closed) {vent_open_timestep = timestep;}
                        safeDigitalWrite(VENT_VALVE_DO, LOW);
                        Debug.println("vent open");
                        vent_closed = false;
                    }
                }
                if (datapoints_collected < MAX_DATAPOINTS){
                    angle_data[datapoints_collected] = angle;
                    pressure_data[datapoints_collected] = pressure;
                    // velocities[datapoints_collected] = angular_velocity;
                    datapoints_collected++;
                }
                // Ensure that loop step takes 1 ms or more (without this it takes quite a bit less)
                sensor_read_time = micros() - sensor_read_time;
                delay_time = DATA_COLLECT_TIMESTEP - sensor_read_time;
                if (delay_time > 0) {
                    delayMicroseconds(delay_time);
                }
                timestep++;
                swing_length = micros() - fire_time;
            }
            // Close throw valve after 1 second even if target angle not achieved
            if (throw_open) {throw_close_timestep = timestep;}
            safeDigitalWrite(THROW_VALVE_DO, LOW);
            delay(10);
            // Open vent valve after 1 second even if target angle not achieved
            if (vent_closed) {vent_open_timestep = timestep;}
            safeDigitalWrite(VENT_VALVE_DO, LOW);
            magOff();
        }
        Debug.println("finished");
        // Send buffered throw data over serial
        for (uint16_t i = 0; i < datapoints_collected; i++) {
            Debug.print("data");
            Debug.print("\t");
            Debug.print(angle_data[i], DEC);
            Debug.print("\t");
            Debug.println(pressure_data[i]);
        }
        Debug.print("timestep\t");
        Debug.println(DATA_COLLECT_TIMESTEP);
        Debug.print("throw_close_timestep\t");
        Debug.println(throw_close_timestep);
        Debug.print("vent_open_timestep\t");
        Debug.println(vent_open_timestep);
    }
}

// use retract motor to gently move hammer to forward position to safe it for approach
#define GENTLE_THROW_BEGIN_ANGLE_MIN 20
#define GENTLE_THROW_STOP_ANGLE 142
#define GENTLE_THROW_COMPLETE_ANGLE 142
#define GENTLE_THROW_TIMEOUT 5000000L
void gentleFire(){
    uint32_t fire_time;
    uint32_t swing_length = 0;
    uint32_t sensor_read_time;
    uint32_t delay_time;
    uint16_t angle;
    uint16_t start_angle;
    bool angle_read_ok;
    int16_t angular_velocity;
    bool velocity_read_ok;
    
    if (weaponsEnabled()){
        bool angle_read_ok = readAngle(&angle);
        // if angle data isn't coming back, abort
        while (!angle_read_ok) {
            uint8_t start_attempts = 1;
            if (start_attempts < 5) {
                angle_read_ok = readAngle(&angle);
                start_attempts++;
            } else {
                return;
            }
        }
        fire_time = micros();
        start_angle = angle;
        if (angle > GENTLE_THROW_BEGIN_ANGLE_MIN && angle < GENTLE_THROW_COMPLETE_ANGLE) {
            while (swing_length < GENTLE_THROW_TIMEOUT && angle < GENTLE_THROW_STOP_ANGLE) {
                angle_read_ok = readAngle(&angle);
                swing_length = micros() - fire_time;
                DriveSerial.println("@05!G -100");  // start motor to aid meshing
                safeDigitalWrite(RETRACT_VALVE_DO, HIGH);
                DriveSerial.println("@05!G -300");
                sensor_read_time = micros() - sensor_read_time;
                delay_time = 1000 - sensor_read_time;
                if (delay_time > 0) {
                    delayMicroseconds(delay_time);
                }
                swing_length = micros() - fire_time;
            }
        }
        safeDigitalWrite(RETRACT_VALVE_DO, LOW);
    }
}

void flameStart(){
    if (weaponsEnabled()){
        safeDigitalWrite(PROPANE_DO, HIGH);
    }
}

void flameEnd(){
    // seems like this shouldn't require enable, even though disable should close valve itself
    digitalWrite(PROPANE_DO, LOW);
}

void magOn(){
    if (weaponsEnabled()){
        safeDigitalWrite(MAG1_DO, HIGH);
        safeDigitalWrite(MAG2_DO, HIGH);
    }
}

void magOff(){
    digitalWrite(MAG1_DO, LOW);
    digitalWrite(MAG2_DO, LOW);
}

void valveSafe(){
    // Safing code deliberately does not use safeDigitalWrite since it should always go through.
    digitalWrite(ENABLE_VALVE_DO, LOW);
    digitalWrite(THROW_VALVE_DO, LOW);
    digitalWrite(VENT_VALVE_DO, LOW);
    digitalWrite(RETRACT_VALVE_DO, LOW);
    pinMode(ENABLE_VALVE_DO, OUTPUT);
    pinMode(THROW_VALVE_DO, OUTPUT);
    pinMode(VENT_VALVE_DO, OUTPUT);
    pinMode(RETRACT_VALVE_DO, OUTPUT);
}

void valveEnable(){
    // Assumes safe() has already been called beforehand, to set pin modes.
    safeDigitalWrite(ENABLE_VALVE_DO, HIGH);
}

void flameSafe(){
    digitalWrite(IGNITER_DO, LOW);
    digitalWrite(PROPANE_DO, LOW);
    pinMode(IGNITER_DO, OUTPUT);
    pinMode(PROPANE_DO, OUTPUT);
}

void flameEnable(){
    // Assumes safe() has already been called beforehand, to set pin modes.
    safeDigitalWrite(IGNITER_DO, HIGH);
}
void magnetSafe(){
    digitalWrite(MAG1_DO, LOW);
    digitalWrite(MAG2_DO, LOW);
    pinMode(MAG1_DO, OUTPUT);
    pinMode(MAG2_DO, OUTPUT);
}

