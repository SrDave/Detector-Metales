#include <Arduino.h>
#include "Preferences.h"
#include "M5Stack.h"
#include "math.h"
#include "M5_BMM150.h"
#include "M5_BMM150_DEFS.h"

#define CIRCULAR_BUFFER_LEN 10

Preferences prefs;

struct bmm150_dev dev;
bmm150_mag_data mag_offset;
bmm150_mag_data mag_scale;
bmm150_mag_data mag_delta;
bmm150_mag_data mag_max;
bmm150_mag_data mag_min;
TFT_eSprite img = TFT_eSprite(&M5.Lcd);
float maximo;
float minimo;

typedef struct {
    int head;
    int tail;
    float values[CIRCULAR_BUFFER_LEN];
} circular_buffer;

circular_buffer bufferX;
circular_buffer bufferY;
circular_buffer bufferZ;

void value_clear(circular_buffer *buf) {
    buf->head = 0;
    buf->tail = 0;
    memset(buf->values, 0, sizeof(buf->values));
}

void value_queue(circular_buffer *buf, float value) {
    buf->values[buf->head] = value;
    buf->head = (buf->head + 1) % CIRCULAR_BUFFER_LEN;
    if (buf->head == buf->tail) {
        buf->tail = (buf->tail + 1) % CIRCULAR_BUFFER_LEN;
    }
}

float value_average(const circular_buffer *buf) {
    float sum = 0;
    int count = 0;
    int index = buf->tail;
    while (index != buf->head) {
        sum += buf->values[index];
        index = (index + 1) % CIRCULAR_BUFFER_LEN;
        count++;
    }
    if (count > 0) {
        return sum / count;
    } else {
        return 0;
    }
}

int8_t i2c_read(uint8_t dev_id, uint8_t reg_addr, uint8_t *read_data, uint16_t len){
    if(M5.I2C.readBytes(dev_id, reg_addr, len, read_data)){
        return BMM150_OK;
    } else {
        return BMM150_E_DEV_NOT_FOUND;
    }
}

int8_t i2c_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *read_data, uint16_t len){
    if(M5.I2C.writeBytes(dev_id, reg_addr, read_data, len)){
        return BMM150_OK;
    } else {
        return BMM150_E_DEV_NOT_FOUND;
    }
}

int8_t bmm150_initialization(){
    int8_t rslt = BMM150_OK;

    dev.dev_id = 0x10;
    dev.intf = BMM150_I2C_INTF;
    dev.read = i2c_read;
    dev.write = i2c_write;
    dev.delay_ms = delay;

    mag_max.x = -2000;
    mag_max.y = -2000;
    mag_max.z = -2000;

    mag_min.x = 2000;
    mag_min.y = 2000;
    mag_min.z = 2000;

    rslt = bmm150_init(&dev);
    dev.settings.pwr_mode = BMM150_NORMAL_MODE;
    rslt |= bmm150_set_op_mode(&dev);
    dev.settings.preset_mode = BMM150_PRESETMODE_ENHANCED;
    rslt |= bmm150_set_presetmode(&dev);

    return rslt;
}

void bmm150_offset_save(){
    prefs.begin("bmm150", false);
    prefs.putBytes("offset", (uint8_t *)&mag_offset, sizeof(bmm150_mag_data));
    prefs.putBytes("scale", (uint8_t *)&mag_scale, sizeof(bmm150_mag_data));
    prefs.end();
}

void bmm150_offset_load(){
    if(prefs.begin("bmm150", true)){
        prefs.getBytes("offset", (uint8_t *)&mag_offset, sizeof(bmm150_mag_data));
        prefs.getBytes("scale", (uint8_t *)&mag_scale, sizeof(bmm150_mag_data));
        prefs.end();
        Serial.println("bmm150 load offset finish....");
    } else {
        Serial.println("bmm150 load offset failed....");
    }
}

void reproducirTono(float diferenciaZ) {
    float porcentajeMinimo = 1.05;
    float porcentajeMaximo = 0.95;

    if ((diferenciaZ > minimo * porcentajeMinimo) && (diferenciaZ < maximo * porcentajeMaximo)) {
        float frecuencia = map(diferenciaZ, minimo, maximo, 1000, 4000);
        M5.Speaker.tone(frecuencia, 1000);
    } else {
        M5.Speaker.mute();
    }
}

void setup() {
    M5.begin(true, false, true, false);
    M5.Power.begin();
    Wire.begin(21, 22, 400000UL);

    img.setColorDepth(1);
    img.setTextColor(TFT_WHITE);
    img.createSprite(320, 240);
    img.setBitmapColor(TFT_WHITE, 0);

    if(bmm150_initialization() != BMM150_OK){
        img.fillSprite(0);
        img.drawCentreString("BMM150 init failed", 160, 110, 4);
        img.pushSprite(0, 0);
        for(;;){
            delay(100);
        }
    }
    M5.Speaker.setVolume(6);
    bmm150_offset_load();

    value_clear(&bufferX);
    value_clear(&bufferY);
    value_clear(&bufferZ);
}

void bmm150_calibrate(uint32_t calibrate_time) {
    uint32_t calibrate_timeout = 0;

    calibrate_timeout = millis() + calibrate_time;
    Serial.printf("Go calibrate, use %d ms \r\n", calibrate_time);
    Serial.printf("running ...");

    while (calibrate_timeout > millis()) {
        bmm150_read_mag_data(&dev);
        if (dev.data.x) {
            mag_min.x = (dev.data.x < mag_min.x) ? dev.data.x : mag_min.x;
            mag_max.x = (dev.data.x > mag_max.x) ? dev.data.x : mag_max.x;
        }
        if (dev.data.y) {
            mag_max.y = (dev.data.y > mag_max.y) ? dev.data.y : mag_max.y;
            mag_min.y = (dev.data.y < mag_min.y) ? dev.data.y : mag_min.y;
        }
        if (dev.data.z) {
            mag_min.z = (dev.data.z < mag_min.z) ? dev.data.z : mag_min.z;
            mag_max.z = (dev.data.z > mag_max.z) ? dev.data.z : mag_max.z;
        }
        delay(100);
    }

    mag_offset.x = (mag_max.x + mag_min.x) / 2;
    mag_offset.y = (mag_max.y + mag_min.y) / 2;
    mag_offset.z = (mag_max.z + mag_min.z) / 2;

    mag_delta.x = (mag_max.x - mag_min.x) / 2;
    mag_delta.y = (mag_max.y - mag_min.y) / 2;
    mag_delta.z = (mag_max.z - mag_min.z) / 2;
    float delta = (mag_delta.x + mag_delta.y + mag_delta.z) / 3;
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
    M5.update();
    bmm150_read_mag_data(&dev);

    float z = (dev.data.z - mag_offset.z) * mag_scale.z;

    value_queue(&bufferX, dev.data.x);
    value_queue(&bufferY, dev.data.y);
    value_queue(&bufferZ, z);

    float promedioX = value_average(&bufferX);
    float promedioY = value_average(&bufferY);
    float promedioZ = value_average(&bufferZ);

    float diferenciaZ = (promedioZ - minimo);

    float head_dir = atan2(promedioX - mag_offset.x, promedioY - mag_offset.y) * 180.0 / M_PI;

    Serial.printf("Magnetometer data, heading %.2f\n", head_dir);
    Serial.printf("MAG X : %.2f \t MAG Y : %.2f \t MAG Z : %.2f \n", promedioX, promedioY, promedioZ);
    Serial.printf("MID X : %.2f \t MID Y : %.2f \t MID Z : %.2f \n", mag_offset.x, mag_offset.y, mag_offset.z);
    Serial.printf("Mag_Scale_Z : %.2f \t Z : %.2f \t MINIMO : %2f \t MAXIMO : %2f", mag_scale.z, z, minimo, maximo);

    img.fillSprite(0);
    sprintf(text_string, "MAG Z: %.2f", z);
    img.drawString(text_string, 10, 30, 4);
    sprintf(text_string, "HEAD Angle: %.2f", head_dir);
    img.drawString(text_string, 10, 60, 4);
    sprintf(text_string, "Z Compensado: %.2f", promedioZ);
    img.drawString(text_string, 10, 90, 4);
    sprintf(text_string, "MINIMO : %.2f", minimo);
    img.drawString(text_string, 10, 120, 4);
    sprintf(text_string, "MAXIMO : %.2f", maximo);
    img.drawString(text_string, 10, 150, 4);
    sprintf(text_string, "DiferenciaZ : %.2f", diferenciaZ);
    img.drawString(text_string, 10, 180, 4);
    img.pushSprite(0, 0);

    if (M5.BtnA.wasPressed()) {
        img.fillSprite(0);
        img.drawCentreString("Flip + rotate core calibration", 160, 110, 4);
        img.pushSprite(0, 0);

        bmm150_calibrate(10000);
    } else if (M5.BtnB.wasPressed()) {
        img.fillSprite(0);
        img.drawCentreString("MIN Saved Value", 160, 150, 4);
        img.pushSprite(0, 0);
        if (promedioZ) {
            minimo = promedioZ;
        }
        delay(2000);
    } else if (M5.BtnC.wasPressed()) {
        img.fillSprite(0);
        img.drawCentreString("MAX Saved Value", 160, 150, 4);
        img.pushSprite(0, 0);
        if (promedioZ) {
            maximo = promedioZ;
        }
        delay(2000);
    }

    if (maximo && minimo) {
        reproducirTono(diferenciaZ);
    }
    delay(10);  // Cambiado de 100 a 10 para aumentar la frecuencia de muestreo
}
