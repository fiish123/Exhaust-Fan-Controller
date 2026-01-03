#ifndef SENSOR_H
#define SENSOR_H
// 引入I2C通信库
#include <Wire.h>

// 引入Arduino核心库
#include <Arduino.h>

// 引入Sensirion气体指数算法库
#include <NOxGasIndexAlgorithm.h>
#include <SensirionI2CSgp41.h>
#include <SensirionI2cSht4x.h>
#include <VOCGasIndexAlgorithm.h>

bool init_sensor();
void test();
#endif