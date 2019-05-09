#include "Tank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Tank::Tank() {

};

void Tank::begin(){
    // do what we want
    this->init = true;
};

void Tank::setLeftMotorPWM(int ln, short pwm) {
    // do what we want
    if(pwm > 255) {
        pwm = 255;
    } else if(pwm < -255) {
        pwm = -255;
    }

    if(this->init) {
        fputc('\x03', stdout);
        fputc((char)(ln), stdout);
        fputc((char)(ln >> 8), stdout);
        fputc((char)(ln >> 16), stdout);
        fputc((char)(ln >> 24), stdout);
        fputc((char)(pwm), stdout);
        fputc((char)(pwm >> 8), stdout);
        fflush(stdout);

        while(fgetc(stdin) != '\x08');
    }
};

void Tank::setRightMotorPWM(int ln, short pwm) {
    // do what we want
    if(pwm > 255) {
        pwm = 255;
    } else if(pwm < -255) {
        pwm = -255;
    }

    if(this->init) {
        fputc('\x04', stdout);
        fputc((char)(ln), stdout);
        fputc((char)(ln >> 8), stdout);
        fputc((char)(ln >> 16), stdout);
        fputc((char)(ln >> 24), stdout);
        fputc((char)(pwm), stdout);
        fputc((char)(pwm >> 8), stdout);
        fflush(stdout);

        while(fgetc(stdin) != '\x08');
    }
};

void Tank::turnOffMotors(int ln){
    // do what we want
    if(this->init) {
        fputc('\x05', stdout);
        fputc((char)(ln), stdout);
        fputc((char)(ln >> 8), stdout);
        fputc((char)(ln >> 16), stdout);
        fputc((char)(ln >> 24), stdout);
        fflush(stdout);
        while(fgetc(stdin) != '\x08');
    }
};

float Tank::readDistanceSensor(int ln, int id) {
    // do what we want
    if(id > 11) {
        return -1.0;
    }

    if(this->init) {
        fputc('\x06', stdout);
        fputc((char)(ln), stdout);
        fputc((char)(ln >> 8), stdout);
        fputc((char)(ln >> 16), stdout);
        fputc((char)(ln >> 24), stdout);
        fputc((char)(id), stdout);
        fflush(stdout);
        
        char buff[5];
        fgets(buff, 5, stdin);
        return *(float *)buff;
    }
    
    return -1.0;
}