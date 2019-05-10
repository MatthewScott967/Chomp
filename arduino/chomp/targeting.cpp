#include "Arduino.h"
#include "leddar_io.h"
#include "targeting.h"
#include "pins.h"
#include "sensors.h"
#include "drive.h"
#include <stdlib.h>
#include <math.h>
#include "imu.h"
#include "telem.h"
#include "utils.h"


static uint8_t segmentObjects(const Detection (&min_detections)[LEDDAR_SEGMENTS],
                              uint32_t now,
                              Object (&objects)[8]);

static void saveObjectSegmentationParameters();

static int8_t selectObject(const Object (&objects)[8], uint8_t num_objects,
                           const struct Track &tracked_object,
                           int32_t *selected_distance);
struct ObjectSegmentationParameters {
    int32_t min_object_size; // object sizes are in mm
    int32_t max_object_size;   // mm of circumferential size
    int32_t edge_call_threshold; // cm for edge in leddar returns
} __attribute__((packed));

static struct ObjectSegmentationParameters object_params;

struct ObjectSegmentationParameters EEMEM saved_object_params = {
    .min_object_size = 200, // object sizes are in mm
    .max_object_size = 1800,   // mm of circumferential size
    .edge_call_threshold = 60 // cm for edge in leddar returns
};

void trackObject(const Detection (&min_detections)[LEDDAR_SEGMENTS],
                 struct Track& tracked_object) {

    int16_t omegaZ = 0;
    getOmegaZ(&omegaZ);

    uint32_t now = micros();

    Object objects[8];
    uint8_t num_objects = segmentObjects(min_detections, now, objects);

    sendObjectsTelemetry(num_objects, objects);

    if(num_objects>0) {
        tracked_object.predict(now, omegaZ);
        int32_t best_distance;
        uint8_t best_object = selectObject(objects, num_objects, tracked_object, &best_distance);
        uint32_t now = objects[best_object].Time;

        if(tracked_object.wants_update(now, best_distance)) {
           tracked_object.update(objects[best_object], omegaZ);
        } else {
           tracked_object.updateNoObs(now, omegaZ);
        }
        sendTrackingTelemetry(objects[best_object].xcoord(),
                              objects[best_object].ycoord(),
                              objects[best_object].angle(),
                              objects[best_object].radius(),
                              tracked_object.x/16,
                              tracked_object.vx/16,
                              tracked_object.y/16,
                              tracked_object.vy/16);
    // below is called if no objects called in current Leddar return
    } else {
        tracked_object.updateNoObs(micros(), omegaZ);
        sendTrackingTelemetry(0,
                              0,
                              0,
                              0,
                              tracked_object.x/16,
                              tracked_object.vx/16,
                              tracked_object.y/16,
                              tracked_object.vy/16);
    }
}


static uint8_t segmentObjects(const Detection (&min_detections)[LEDDAR_SEGMENTS],
                              uint32_t now,
                              Object (&objects)[8]) {
    // call all objects in frame by detecting edges
    int16_t last_seg_distance = min_detections[0].Distance;
    int16_t right_edge = 0;
    int16_t left_edge = 0;
    uint8_t num_objects = 0;
    // this currently will not call a more distant object obscured by a nearer object, even if both edges of more distant object are visible
    for (uint8_t i = 1; i < 16; i++) {
        int16_t delta = (int16_t) min_detections[i].Distance - last_seg_distance;
        if (delta < -object_params.edge_call_threshold) {
            left_edge = i;
            objects[num_objects].SumDistance = 0;
        } else if (delta > object_params.edge_call_threshold) {
            // call object if there is an unmatched left edge
            if (left_edge >= right_edge) {
                right_edge = i;
                objects[num_objects].LeftEdge = left_edge;
                objects[num_objects].RightEdge = right_edge;
                objects[num_objects].Time = now;
                int16_t size = objects[num_objects].size();
                if(size>object_params.min_object_size && size<object_params.max_object_size) {
                    num_objects++;
                }
            }
        }
        objects[num_objects].SumDistance += min_detections[i].Distance;
        last_seg_distance = min_detections[i].Distance;
    }

    // call object after loop if no matching right edge seen for a left edge-- end of loop can be a right edge. do not call entire FOV an object
    if ((left_edge > 0 && left_edge > right_edge) ||
        (left_edge == 0 && right_edge == 0)){
        right_edge = LEDDAR_SEGMENTS;
        objects[num_objects].MinDistance = min_obj_distance;
        objects[num_objects].MaxDistance = max_obj_distance;
        objects[num_objects].LeftEdge = left_edge;
        objects[num_objects].RightEdge = right_edge;
        objects[num_objects].Time = now;
        int16_t size = objects[num_objects].size();
        if(size>object_params.min_object_size && size<object_params.max_object_size) {
            num_objects++;
        }
    }

    return num_objects;
}

static int8_t selectObject(const Object (&objects)[8], uint8_t num_objects,
                           const struct Track &tracked_object,
                           int32_t *selected_distance) {
    int8_t best_match = 0;
    int32_t best_distance;
    uint32_t now = objects[best_match].Time;
    if(tracked_object.recent_update(now)) {
        // have a track, find the closes detection
        best_distance = tracked_object.distanceSq(objects[best_match]);
        for (uint8_t i = 1; i < num_objects; i++) {
            int32_t distance;
            distance = tracked_object.distanceSq(objects[i]);
            if (distance < best_distance) {
                best_distance = distance;
                best_match = i;
            }
        }
    } else {
        // no track, pick the nearest object
        best_distance = objects[best_match].radius();
        best_distance *= best_distance;
        for (uint8_t i = 1; i < num_objects; i++) {
            int32_t distance;
            distance = objects[i].radius();
            distance *= distance;
            if (distance < best_distance) {
                best_distance = distance;
                best_match = i;
            }
        }
    }
    *selected_distance = best_distance;
    return best_match;
}

void setObjectSegmentationParams(int16_t p_min_object_size,
                                 int16_t p_max_object_size,
                                 int16_t p_edge_call_threshold){
    object_params.min_object_size    = p_min_object_size;
    object_params.max_object_size    = p_max_object_size;
    object_params.edge_call_threshold= p_edge_call_threshold;
    saveObjectSegmentationParameters();
}

static void saveObjectSegmentationParameters() {
    eeprom_write_block(&object_params, &saved_object_params,
                       sizeof(struct ObjectSegmentationParameters));
}


void restoreObjectSegmentationParameters() {
    eeprom_read_block(&object_params, &saved_object_params,
                       sizeof(struct ObjectSegmentationParameters));
}
