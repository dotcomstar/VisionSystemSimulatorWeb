#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <cjson/cJSON.h>
#include <math.h>

#include "simulator.h"
#include "node.h"
#include "vs.h"

#define SENSOR_RANGE 1.0f
#define PI 3.1415926535f
#define BUFF_SIZE 258
#define EPSILON 0.000001f
#define ROTATIONS_PER_SECOND 0.25f
#define max(x1,x2) ((x1) > (x2) ? (x1) : (x2))
#define min(x1,x2) ((x1) < (x2) ? (x1) : (x2))

char buffer [BUFF_SIZE];
unsigned short buffer_pos = 0;

float cross_product(struct coordinate v, struct coordinate w) {
    return v.x * w.y - v.y * w.x;
}

float dot_product(struct coordinate v, struct coordinate w) {
    return v.x * w.x + v.y * w.y;
}

struct coordinate * get_intersection(struct line l1, struct line l2) {
    struct coordinate p = l1.p1;
    struct coordinate q = l2.p1;
    struct coordinate r = l1.p2; r.x -= p.x; r.y -= p.y;
    struct coordinate s = l2.p2; s.x -= q.x; s.y -= q.y;

    float c_rs = cross_product(r, s);
    struct coordinate qminp; qminp.x = q.x - p.x; qminp.y = q.y - p.y;
    float c_qminpr = cross_product(qminp, r);
    float c_qminps = cross_product(qminp, s);

    if(fabs(c_rs) < EPSILON) {
        if(fabs(c_qminpr) < EPSILON) {
            // colinear --
            float d_qminpr = dot_product(qminp, r);
            float d_rr = dot_product(r, r);
            float d_sr = dot_product(s, r);

            float t0 = d_qminpr / d_rr;
            float t1 = t0 + d_sr / d_rr;

            if(d_sr < 0) {
                float temp = t0;
                t0 = t1; t1 = temp;
            }

            // check intersection with 0-1
            if(t0 >= 0 && t0 <= 1) {
                // t0 has intersection
                struct coordinate *ret = (struct coordinate *)malloc(1 * sizeof(struct coordinate));
                ret->x = p.x + t0 * r.x;
                ret->y = p.y + t0 * r.y;

                return ret;
            }

            if(t1 >= 0 && t1 <= 1) {
                // t1 has intersection
                struct coordinate *ret = (struct coordinate *)malloc(1 * sizeof(struct coordinate));
                ret->x = p.x + t1 * r.x;
                ret->y = p.y + t1 * r.y;

                return ret;
            }

            return NULL;
        } else {
            // paralell and non-intersecting --
            return NULL;
        }
    } else {
        float t = c_qminps / c_rs;
        float u = c_qminpr / c_rs;

        if((t <= 1.0 && t >= 0.0) && (u <= 1.0 && u >= 0.0)) {
            // intersecting --
            struct coordinate *ret = (struct coordinate *)malloc(1 * sizeof(struct coordinate));
            ret->x = p.x + t * r.x;
            ret->y = p.y + t * r.y;

            return ret;
        } else {
            // non-parallel but not intersecting --
            return NULL;
        }
    }

    return NULL;
}

float distance(struct coordinate a, struct coordinate b) {
    return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2));
}

float read_distance_sensor(struct arena arena, short index) {
    int i, j;

    if(!arena.osv.distance_sensors[index]) {
        return -1.0;
    }

    // we have to get the slope of the front side of the osv first
    float cos_theta = cos(arena.osv.location.theta);
    float sin_theta = sin(arena.osv.location.theta);

    struct coordinate midPointFront;
    midPointFront.x = arena.osv.location.x + arena.osv.height / 2 * cos_theta;
    midPointFront.y = arena.osv.location.y + arena.osv.height / 2 * sin_theta;

    struct coordinate a;
    a.x = midPointFront.x - arena.osv.width / 2 * sin_theta;
    a.y = midPointFront.y + arena.osv.width / 2 * cos_theta;

    struct coordinate b;
    b.x = midPointFront.x + arena.osv.width / 2 * sin_theta;
    b.y = midPointFront.y - arena.osv.width / 2 * cos_theta;

    struct coordinate midPointBack;
    midPointBack.x = arena.osv.location.x - arena.osv.height / 2 * cos_theta;
    midPointBack.y = arena.osv.location.y - arena.osv.height / 2 * sin_theta;

    struct coordinate c;
    c.x = midPointBack.x - arena.osv.width / 2 * sin_theta;
    c.y = midPointBack.y + arena.osv.width / 2 * cos_theta;

    struct coordinate d;
    d.x = midPointBack.x + arena.osv.width / 2 * sin_theta;
    d.y = midPointBack.y - arena.osv.width / 2 * cos_theta;

    struct coordinate midPointLeft;
    midPointLeft.x = (a.x + c.x) / 2;
    midPointLeft.y = (a.y + c.y) / 2;

    struct coordinate midPointRight;
    midPointRight.x = (b.x + d.x) / 2;
    midPointRight.y = (b.y + d.y) / 2;

    struct coordinate sensor_locations[12] = {a, midPointFront, b, b, midPointRight, d, d, midPointBack, c, c, midPointLeft, a};

    int sideIndex = index / 3;
    float orientation = arena.osv.location.theta - sideIndex * PI / 2;

    struct coordinate endPoint;
    endPoint.x = sensor_locations[index].x + SENSOR_RANGE * cos(orientation);
    endPoint.y = sensor_locations[index].y + SENSOR_RANGE * sin(orientation);

    struct line sensor_trace;
    sensor_trace.p1.x = sensor_locations[index].x;
    sensor_trace.p1.y = sensor_locations[index].y;
    sensor_trace.p2.x = endPoint.x;
    sensor_trace.p2.y = endPoint.y;

    float minimum_distance = 1.0;

    for(i = 0; i < arena.num_obstacles; i++) {
        // for each of the arena.obstacles[i]s
        struct line right;
        struct coordinate r1, r2;
        r1.x = arena.obstacles[i].location.x + arena.obstacles[i].width;
        r1.y = arena.obstacles[i].location.y;
        r2.x = arena.obstacles[i].location.x + arena.obstacles[i].width;
        r2.y = arena.obstacles[i].location.y - arena.obstacles[i].height;
        right.p1 = r1; right.p2 = r2;

        struct line bottom;
        struct coordinate b1, b2;
        b1.x = arena.obstacles[i].location.x;
        b1.y = arena.obstacles[i].location.y - arena.obstacles[i].height;
        b2.x = arena.obstacles[i].location.x + arena.obstacles[i].width;
        b2.y = arena.obstacles[i].location.y - arena.obstacles[i].height;
        bottom.p1 = b1; bottom.p2 = b2;

        struct line left;
        struct coordinate l1, l2;
        l1.x = arena.obstacles[i].location.x;
        l1.y = arena.obstacles[i].location.y - arena.obstacles[i].height;
        l2.x = arena.obstacles[i].location.x;
        l2.y = arena.obstacles[i].location.y;
        left.p1 = l1; left.p2 = l2;

        struct line top;
        struct coordinate t1, t2;
        t1.x = arena.obstacles[i].location.x;
        t1.y = arena.obstacles[i].location.y;
        t2.x = arena.obstacles[i].location.x + arena.obstacles[i].width;
        t2.y = arena.obstacles[i].location.y;
        top.p1 = t1; top.p2 = t2;
        
        struct line obstacle_sides[4] = {right, bottom, left, top};

        for(j = 0; j < 4; j++) {
        	struct coordinate *intersection_point = get_intersection(obstacle_sides[j], sensor_trace);
            if(intersection_point != NULL) {
                //todo: need to find point on obstacle that is closest to the sensor location, and then find the distance
                minimum_distance = min(minimum_distance, distance(sensor_locations[index], *intersection_point));
                free(intersection_point);
            }
        }
    }

    return minimum_distance;
}

int check_for_collisions(struct arena *arena) {
    int i, j, k;
    // struct coordinates a,b,c,d represent the four corners of the OSV
    double cos_theta = cos(arena->osv.location.theta);
    double sin_theta = sin(arena->osv.location.theta);

    struct coordinate coordinate_front;
    coordinate_front.x = arena->osv.location.x + arena->osv.width / 2 * cos_theta;
    coordinate_front.y = arena->osv.location.y + arena->osv.width / 2 * sin_theta;

    struct coordinate a;
    a.x = coordinate_front.x - arena->osv.height / 2 * sin_theta;
    a.y = coordinate_front.y + arena->osv.height / 2 * cos_theta;

    struct coordinate b;
    b.x = coordinate_front.x + arena->osv.height / 2 * sin_theta;
    b.y = coordinate_front.y - arena->osv.height / 2 * cos_theta;

    struct coordinate coordinate_back;
    coordinate_back.x = arena->osv.location.x - arena->osv.width / 2 * cos_theta;
    coordinate_back.y = arena->osv.location.y - arena->osv.width / 2 * sin_theta;

    struct coordinate c;
    c.x = coordinate_back.x - arena->osv.height / 2 * sin_theta;
    c.y = coordinate_back.y + arena->osv.height / 2 * cos_theta;

    struct coordinate d;
    d.x = coordinate_back.x + arena->osv.height / 2 * sin_theta;
    d.y = coordinate_back.y - arena->osv.height / 2 * cos_theta;

    struct line front_osv;
    front_osv.p1 = a;
    front_osv.p2 = b;
    
    struct line left_osv;
    left_osv.p1 = a;
    left_osv.p2 = c;

    struct line back_osv;
    back_osv.p1 = c;
    back_osv.p2 = d;

    struct line right_osv;
    right_osv.p1 = b;
    right_osv.p2 = d;

    struct line osv_sides[4] = {front_osv, left_osv, back_osv, right_osv};

    for(i = 0; i < arena->num_obstacles; i++) {
        // for each of the obstacles
        struct line right;
        struct coordinate r1, r2;
        r1.x = arena->obstacles[i].location.x + arena->obstacles[i].width;
        r1.y = arena->obstacles[i].location.y;
        r2.x = arena->obstacles[i].location.x + arena->obstacles[i].width;
        r2.y = arena->obstacles[i].location.y - arena->obstacles[i].height;
        right.p1 = r1; right.p2 = r2;

        struct line bottom;
        struct coordinate b1, b2;
        b1.x = arena->obstacles[i].location.x;
        b1.y = arena->obstacles[i].location.y - arena->obstacles[i].height;
        b2.x = arena->obstacles[i].location.x + arena->obstacles[i].width;
        b2.y = arena->obstacles[i].location.y - arena->obstacles[i].height;
        bottom.p1 = b1; bottom.p2 = b2;

        struct line left;
        struct coordinate l1, l2;
        l1.x = arena->obstacles[i].location.x;
        l1.y = arena->obstacles[i].location.y - arena->obstacles[i].height;
        l2.x = arena->obstacles[i].location.x;
        l2.y = arena->obstacles[i].location.y;
        left.p1 = l1; left.p2 = l2;

        struct line top;
        struct coordinate t1, t2;
        t1.x = arena->obstacles[i].location.x;
        t1.y = arena->obstacles[i].location.y;
        t2.x = arena->obstacles[i].location.x + arena->obstacles[i].width;
        t2.y = arena->obstacles[i].location.y;
        top.p1 = t1; top.p2 = t2;
        
        struct line obstacle_sides[4] = {right, bottom, left, top};

        for(j = 0; j < 4; j++) {
            for(k = 0; k < 4; k++) {
                struct coordinate *intersection_point = get_intersection(osv_sides[k], obstacle_sides[j]);
                if(intersection_point != NULL) {
                    free(intersection_point);    
                    return 1;
                }
            }
        }
    }

    // now we want to define the walls:
    struct line right;
    struct coordinate r1, r2;
    r1.x = 4.0;
    r1.y = 0.0;
    r2.x = 4.0;
    r2.y = 2.0;
    right.p1 = r1; right.p2 = r2;
    
    struct line bottom;
    struct coordinate b1, b2;
    b1.x = 0.0;
    b1.y = 0.0;
    b2.x = 4.0;
    b2.y = 0.0;
    bottom.p1 = b1; bottom.p2 = b2;

    struct line left;
    struct coordinate l1, l2;
    l1.x = 0.0;
    l1.y = 0.0;
    l2.x = 0.0;
    l2.y = 2.0;
    bottom.p1 = l1; bottom.p2 = l2;

    struct line top;
    struct coordinate t1, t2;
    t1.x = 0.0;
    t1.y = 2.0;
    t2.x = 4.0;
    t2.y = 2.0;
    top.p1 = t1; top.p2 = t2;

    struct line walls[4] = {right, bottom, left, top};

    // need to check right and left sides of OSV in case OSV is perpendicular to wall
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 4; j++) {
            struct coordinate *intersection_point = get_intersection(osv_sides[j], walls[i]);
            if(intersection_point != NULL) {
                free(intersection_point);
                return 1;
            }
        }
    }

    return 0;
}

void update_osv(struct arena *arena) {
    struct coordinate prev_location;
    prev_location.x = arena->osv.location.x;
    prev_location.y = arena->osv.location.y;
    prev_location.theta = arena->osv.location.theta;

    double speed = (arena->osv.right_motor_pwm + arena->osv.left_motor_pwm) / (255.0 * 50.0);

    arena->osv.location.x = arena->osv.location.x + speed * cos(arena->osv.location.theta);
    arena->osv.location.y = arena->osv.location.y + speed * sin(arena->osv.location.theta);

    arena->osv.location.theta += 2 * PI * ROTATIONS_PER_SECOND / 50 * (arena->osv.right_motor_pwm - arena->osv.left_motor_pwm) / 255.0;

    if(check_for_collisions(arena)) {
        arena->osv.location.x = prev_location.x;
        arena->osv.location.y = prev_location.y;
        arena->osv.location.theta = prev_location.theta;
    }
}

struct node * process_command(struct node *in, struct process p, struct arena *arena) {
    char opcode;
    int i;
    unsigned char ack_code = '\x07';
    struct node *curr, *next;
    buffer_pos = 0;

    if(in == NULL || in->size == 0) {
        return in;
    }

    curr = in;
    // read in all of the available data into the buffer
    while(curr != NULL && (buffer_pos < BUFF_SIZE)) {
        for(i = 0; i < curr->size; i++) {
            buffer[i + buffer_pos] = (curr->data)[i];
        }

        buffer_pos += curr->size;
        curr = curr->next;
    }

    // we are at the beginning of a message, check for opcode
    opcode = buffer[0];
    if(opcode == 0x00) {
        // Enes100.begin() message
        // receives: 1 byte opcode
        // returns: 3 floats
        write(p.output_fd, &(arena->destination.x), sizeof(float));
        write(p.output_fd, &(arena->destination.y), sizeof(float));
        write(p.output_fd, &(arena->destination.theta), sizeof(float));
    } else if(opcode == 0x01) {
        // updateLocation() message
        // receives: 1 byte opcode
        // returns: 3 floats
        write(p.output_fd, &(arena->osv.location.x), sizeof(float));
        write(p.output_fd, &(arena->osv.location.y), sizeof(float));
        write(p.output_fd, &(arena->osv.location.theta), sizeof(float));  
    } else if(opcode == 0x02) {
        // println() message
        // receives: 1 byte opcode, 1 byte length, length number of characters
        // returns: 1 byte ack
        if(in->size == 1 || buffer[1] < buffer_pos - 2) {
            return in;
        } else {
            write(p.output_fd, &ack_code, sizeof(unsigned char));
        }
    } else if(opcode == 0x03) {
        // Tank.setLeftMotorPWM()
        // receives: 1 byte opcode, 2 byte pwm value
        // returns: 1 byte ack
        if(in->size < 3) {
            return in;
        } else {
            write(p.output_fd, &ack_code, sizeof(unsigned char));
            arena->osv.left_motor_pwm = (buffer[1] << 8) + buffer[2];
        }
    } else if(opcode == 0x04) {
        // Tank.setRightMotorPWM()
        // receives: 1 byte opcode, 2 byte pwm value
        // returns: 1 byte ack
        if(in->size < 3) {
            return in;
        } else {
            write(p.output_fd, &ack_code, sizeof(unsigned char));
            arena->osv.right_motor_pwm = (buffer[1] << 8) + buffer[2];
        }
    } else if(opcode == 0x05) {
        // Tank.turnOffMotors()
        // receives: 1 byte opcode
        // returns: 1 byte ack
        arena->osv.left_motor_pwm = 0;
        arena->osv.right_motor_pwm = 0;
        write(p.output_fd, &ack_code, sizeof(unsigned char));
    } else if(opcode == 0x06) {
        // Tank.readDistanceSensors()
        // receives: 1 byte opcode, 1 byte index
        // returns: 4 byte float
        if(buffer_pos < 2) {
            return in;
        } else {
            float distVal = read_distance_sensor(*arena, (short)buffer[1]);
            write(p.output_fd, &distVal, sizeof(float));
        }
    } else {
        // error("Invalid opcode\n");
    }

    // free all of the data
    while (curr != NULL) {
        next = curr->next;
        free(curr->data);
        free(curr);
        curr = next;
    }

    return NULL;    
}

struct node * frame(struct node *in, struct process p, struct arena *arena, int frame_no) {
    update_osv(arena);
    struct node * ret_node = process_command(in, p, arena);

    cJSON *root = cJSON_CreateObject();
    cJSON *osv = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "frame_no", frame_no);
    cJSON_AddNumberToObject(osv, "x", arena->osv.location.x);
    cJSON_AddNumberToObject(osv, "y", arena->osv.location.y);
    cJSON_AddNumberToObject(osv, "theta", arena->osv.location.theta);
    cJSON_AddItemToObject(root, "osv", osv);

    printf("%s,", cJSON_Print(root));
    cJSON_Delete(root);

    return ret_node;
}
