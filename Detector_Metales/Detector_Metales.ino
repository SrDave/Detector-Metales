/*
*******************************************************************************
* Copyright (c) 2021 by M5Stack
*                  Equipped with M5Core sample source code
*                          配套  M5Core 示例源代码
* Visit the website for more information：https://docs.m5stack.com/en/core/gray
* 获取更多资料请访问：https://docs.m5stack.com/zh_CN/core/gray
*
* describe：bmm150--Magnetometer 三轴磁力计
* date：2021/7/21
*******************************************************************************
*/

#include <Arduino.h>
#include "Preferences.h"
#include "M5Stack.h"
#include "math.h"
#include "M5_BMM150.h"
#include "M5_BMM150_DEFS.h"

Preferences prefs;

struct bmm150_dev dev;
bmm150_mag_data mag_offset; // Compensation magnetometer float data storage 储存补偿磁强计浮子数据
bmm150_mag_data mag_scale;
bmm150_mag_data mag_delta;
bmm150_mag_data mag_max;
bmm150_mag_data mag_min;
TFT_eSprite img = TFT_eSprite(&M5.Lcd);
float maximo;
float minimo;


int8_t i2c_read(uint8_t dev_id, uint8_t reg_addr, uint8_t *read_data, uint16_t len){
    if(M5.I2C.readBytes(dev_id, reg_addr, len, read_data)){ // Check whether the device ID, address, data exist.
        return BMM150_OK;                                   //判断器件的Id、地址、数据是否存在
    }else{
        return BMM150_E_DEV_NOT_FOUND;
    }
}

int8_t i2c_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *read_data, uint16_t len){
    if(M5.I2C.writeBytes(dev_id, reg_addr, read_data, len)){    //Writes data of length len to the specified device address.
        return BMM150_OK;                                       //向指定器件地址写入长度为len的数据
    }else{
        return BMM150_E_DEV_NOT_FOUND;
    }
}

int8_t bmm150_initialization(){
    int8_t rslt = BMM150_OK;

    dev.dev_id = 0x10;  //Device address setting.  设备地址设置
    dev.intf = BMM150_I2C_INTF; //SPI or I2C interface setup.  SPI或I2C接口设置
    dev.read = i2c_read;    //Read the bus pointer.  读总线指针
    dev.write = i2c_write;  //Write the bus pointer.  写总线指针
    dev.delay_ms = delay;

    // Set the maximum range range
    //设置最大范围区间
    mag_max.x = -2000;
    mag_max.y = -2000;
    mag_max.z = -2000;

    // Set the minimum range
    //设置最小范围区间
    mag_min.x = 2000;
    mag_min.y = 2000;
    mag_min.z = 2000;

    rslt = bmm150_init(&dev);   //Memory chip ID.  存储芯片ID
    dev.settings.pwr_mode = BMM150_NORMAL_MODE;
    rslt |= bmm150_set_op_mode(&dev);   //Set the sensor power mode.  设置传感器电源工作模式
    dev.settings.preset_mode = BMM150_PRESETMODE_ENHANCED;
    rslt |= bmm150_set_presetmode(&dev);    //Set the preset mode of .  设置传感器的预置模式
    return rslt;
}

void bmm150_offset_save(){  //Store the data.  存储bmm150的数据
    prefs.begin("bmm150", false);
    prefs.putBytes("offset", (uint8_t *)&mag_offset, sizeof(bmm150_mag_data));
    prefs.putBytes("scale", (uint8_t *)&mag_scale, sizeof(bmm150_mag_data));
    prefs.end();
}

void bmm150_offset_load(){  //load the data.  加载bmm150的数据
    if(prefs.begin("bmm150", true)){
        prefs.getBytes("offset", (uint8_t *)&mag_offset, sizeof(bmm150_mag_data));
        prefs.getBytes("scale", (uint8_t *)&mag_scale, sizeof(bmm150_mag_data));
        prefs.end();
        Serial.println("bmm150 load offset finish....");
    }else{
        Serial.println("bmm150 load offset failed....");
    }
}

void reproducirTono(float diferenciaZ) {
    // Establecer los porcentajes de referencia mínimo y máximo
    float porcentajeMinimo = 1.05;  // 5% superior al valor de referencia mínimo
    float porcentajeMaximo = 0.95;  // 95% del valor de referencia máximo

    // Comprobar si el valor diferencial del campo magnético en Z supera el 5% del valor mínimo
    if ((diferenciaZ   > minimo * porcentajeMinimo) && (diferenciaZ < maximo * porcentajeMaximo)) {
        // Calcular la frecuencia del tono en función de la diferencia en Z
        float frecuencia = map(diferenciaZ, minimo, maximo, 1000, 4000);

        // Reproducir el tono en el altavoz
        M5.Speaker.tone(frecuencia, 500);  // 1000 es la duración en ms
    } else {
        // Detener el tono si el valor diferencial no supera el 5% del valor mínimo
        M5.Speaker.mute();
    }
}


void setup() {
    M5.begin(true, false, true, false); //Init M5Core(Initialize LCD, serial port).  初始化 M5Core（初始化LCD、串口）
    M5.Power.begin();   //Init Power module.  初始化电源设置
    Wire.begin(21, 22, 400000UL); //Set the frequency of the SDA SCL.  设置SDA和SCL的频率

    img.setColorDepth(1);   //Set bits per pixel for colour.  设置色深为1
    img.setTextColor(TFT_WHITE);    //Set the font foreground colour (background is.  设置字体的前景色为TFT_WHITE
    img.createSprite(320, 240); //Create a sprite (bitmap) of defined width and height 创建一个指定宽度和高度的Sprite图
    img.setBitmapColor(TFT_WHITE, 0);   //Set the foreground and background colour.  设置位图的前景色和背景颜色

    if(bmm150_initialization() != BMM150_OK){
        img.fillSprite(0);  //Fill the whole sprite with defined colour.  用定义的颜色填充整个Sprite图
        img.drawCentreString("BMM150 init failed", 160, 110, 4);    //Use font 4 in (160,110)draw string.  使用字体4在(160,110)处绘制字符串
        img.pushSprite(0, 0);   //Push the sprite to the TFT at 0, 0.  将Sprite图打印在(0,0)处
        for(;;){
            delay(100); //delay 100ms.  延迟100ms
        }
    }
    M5.Speaker.setVolume(8);
    bmm150_offset_load();
}

void bmm150_calibrate(uint32_t calibrate_time){ //bbm150 data calibrate.  bbm150数据校准
    uint32_t calibrate_timeout = 0;

    calibrate_timeout = millis() + calibrate_time;
    Serial.printf("Go calibrate, use %d ms \r\n", calibrate_time);  //The serial port outputs formatting characters.  串口输出格式化字符
    Serial.printf("running ...");

    while (calibrate_timeout > millis()){
        bmm150_read_mag_data(&dev); //read the magnetometer data from registers.  从寄存器读取磁力计数据
        if(dev.data.x){
            mag_min.x = (dev.data.x < mag_min.x) ? dev.data.x : mag_min.x;
            mag_max.x = (dev.data.x > mag_max.x) ? dev.data.x : mag_max.x;
        }
        if(dev.data.y){
            mag_max.y = (dev.data.y > mag_max.y) ? dev.data.y : mag_max.y;
            mag_min.y = (dev.data.y < mag_min.y) ? dev.data.y : mag_min.y;
        }
        if(dev.data.z){
            mag_min.z = (dev.data.z < mag_min.z) ? dev.data.z : mag_min.z;
            mag_max.z = (dev.data.z > mag_max.z) ? dev.data.z : mag_max.z;
        }
        delay(100);
    }

    mag_offset.x = (mag_max.x + mag_min.x) / 2;
    mag_offset.y = (mag_max.y + mag_min.y) / 2;
    mag_offset.z = (mag_max.z + mag_min.z) / 2;

    mag_delta.x = (mag_max.x - mag_min.x) /2;
    mag_delta.y = (mag_max.y - mag_min.y) /2;
    mag_delta.z = (mag_max.z - mag_min.z) /2;
    float delta = (mag_delta.x + mag_delta.y + mag_delta.z)/3;
    mag_scale.x = delta / mag_delta.x;
    mag_scale.y = delta / mag_delta.y;
    mag_scale.z = delta / mag_delta.z;

    bmm150_offset_save();

    Serial.printf("\n calibrate finish ... \r\n");
    Serial.printf("mag_max.x: %.2f x_min: %.2f \t", mag_max.x, mag_min.x);
    Serial.printf("y_max: %.2f y_min: %.2f \t", mag_max.y, mag_min.y);
    Serial.printf("z_max: %.2f z_min: %.2f \r\n", mag_max.z, mag_min.z);
}

void loop() {
    char text_string[100];
    M5.update();    //Read the press state of the key.  读取按键的状态
    bmm150_read_mag_data(&dev);

    float z = (dev.data.z - mag_offset.z) * mag_scale.z;

    // Calcular el valor diferencial del campo magnético en Z
    float diferenciaZ = (z - minimo);

    float head_dir = atan2(dev.data.x -  mag_offset.x, dev.data.y - mag_offset.y) * 180.0 / M_PI;
    Serial.printf("Magnetometer data, heading %.2f\n", head_dir);
    Serial.printf("MAG X : %.2f \t MAG Y : %.2f \t MAG Z : %.2f \n", dev.data.x, dev.data.y, dev.data.z);
    Serial.printf("MID X : %.2f \t MID Y : %.2f \t MID Z : %.2f \n", mag_offset.x, mag_offset.y, mag_offset.z);
    Serial.printf("Mag_Scale_Z : %.2f \t Z : %.2f \t MINIMO : %2f \t MAXIMO : %2f", mag_scale.z, z,minimo,maximo);

    img.fillSprite(0);
    sprintf(text_string, "MAG Z: %.2f", dev.data.z);
    img.drawString(text_string, 10, 30, 4);
    sprintf(text_string, "HEAD Angle: %.2f", head_dir);
    img.drawString(text_string, 10, 60, 4);
    sprintf(text_string, "Z Compensado: %.2f", z);
    img.drawString(text_string, 10, 90, 4);
    sprintf(text_string, "MINIMO : %.2f", minimo);
    img.drawString(text_string, 10, 120, 4);
    sprintf(text_string, "MAXIMO : %.2f", maximo);
    img.drawString(text_string, 10, 150, 4);
    sprintf(text_string, "DiferenciaZ : %.2f", diferenciaZ);
    img.drawString(text_string, 10, 180, 4);
        /*
    img.drawCentreString("Press BtnA enter calibrate", 160, 150, 4);
    img.drawString(text_string, 10, 180, 4);
    img.drawCentreString("Press BtnB for MIN value", 160, 180, 4);
    img.drawString(text_string, 10, 180, 4);
    img.drawCentreString("Press BtnC for MAX value", 160, 210, 4);
    */
    img.pushSprite(0, 0);

    
    if(M5.BtnA.wasPressed()){
        img.fillSprite(0);
        img.drawCentreString("Flip + rotate core calibration", 160, 110, 4);
        img.pushSprite(0, 0);

        bmm150_calibrate(10000);
    } else if(M5.BtnB.wasPressed()){
        img.fillSprite(0);
        img.drawCentreString("MIN Saved Value", 160, 150, 4);
        img.pushSprite(0, 0);
        if(z){
            minimo = z;}
        delay(2000);
    } else if(M5.BtnC.wasPressed()){
        img.fillSprite(0);
        img.drawCentreString("MAX Saved Value", 160, 150, 4);
        img.pushSprite(0, 0);
        if(z){
            maximo = z;}
        delay(2000);
    }


    // Llamar a la función para reproducir el tono en función de la diferencia en Z
    if (maximo && minimo){
    reproducirTono(diferenciaZ);
    }
    delay(100);
}
