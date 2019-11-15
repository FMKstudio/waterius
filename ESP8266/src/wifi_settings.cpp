
#include "wifi_settings.h"
#include "Logging.h"

#include "Blynk/BlynkConfig.h"
#include <ESP8266WiFi.h>
#include <IPAddress.h>
#include <EEPROM.h>
#include "utils.h"
#include "WateriusHttps.h"

//Конвертируем значение переменных компиляции в строк
#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)
#define VAR_NAME_VALUE(var) #var "="  VALUE(var)

/* Сохраняем конфигурацию в EEPROM */
void storeConfig(const Settings &sett) 
{
    EEPROM.begin(sizeof(sett));
    EEPROM.put(0, sett);
    
    if (!EEPROM.commit())
    {
        LOG_ERROR("CFG", "Config stored FAILED");
    }
    else
    {
        LOG_NOTICE("CFG", "Config stored OK");
    }
}


/* Загружаем конфигурацию в EEPROM. true - успех. */
bool loadConfig(struct Settings &sett) 
{
    EEPROM.begin(sizeof(sett));  //  4 до 4096 байт. с адреса 0x7b000.
    EEPROM.get(0, sett);

    if (sett.crc == FAKE_CRC)  // todo: сделать нормальный crc16
    {
        LOG_NOTICE(FPSTR(S_CFG), FPSTR(S_CRC_OK));

        // Для безопасной работы с буферами,  в библиотеках может не быть проверок
        sett.waterius_host[WATERIUS_HOST_LEN-1] = '\0';
        sett.waterius_key[WATERIUS_KEY_LEN-1] = '\0';
        sett.waterius_email[EMAIL_LEN-1] = '\0';

        sett.blynk_key[BLYNK_KEY_LEN-1] = '\0';
        sett.blynk_host[BLYNK_HOST_LEN-1] = '\0';
        sett.blynk_email[EMAIL_LEN-1] = '\0';
        sett.blynk_email_title[BLYNK_EMAIL_TITLE_LEN-1] = '\0';
        sett.blynk_email_template[BLYNK_EMAIL_TEMPLATE_LEN-1] = '\0'; 

        sett.mqtt_host[MQTT_HOST_LEN-1] = '\0'; 
        sett.mqtt_login[MQTT_LOGIN_LEN-1] = '\0'; 
        sett.mqtt_password[MQTT_PASSWORD_LEN-1] = '\0'; 
        sett.mqtt_topic[MQTT_TOPIC_LEN-1] = '\0'; 

        LOG_NOTICE(FPSTR(S_CFG), "email=" << sett.waterius_email);
        LOG_NOTICE(FPSTR(S_CFG), "host=" << sett.waterius_host << " key=" << sett.waterius_key);
        
        LOG_NOTICE(FPSTR(S_CFG), "host=" << sett.blynk_host << " key=" << sett.blynk_key);
        LOG_NOTICE(FPSTR(S_CFG), "email=" << sett.blynk_email);

        LOG_NOTICE(FPSTR(S_CFG), "host=" << sett.mqtt_host << " port=" << sett.mqtt_port);
        LOG_NOTICE(FPSTR(S_CFG), "login=" << sett.mqtt_login << " pass=" << sett.mqtt_password);
        LOG_NOTICE(FPSTR(S_CFG), "topic=" << sett.mqtt_topic);        
        
        // Всегда одно и тоже будет
        LOG_NOTICE(FPSTR(S_CFG), "channel0_start=" << sett.channel0_start << ", impulses0_start=" << sett.impulses0_start << ", factor=" << sett.liters_per_impuls );
        LOG_NOTICE(FPSTR(S_CFG), "channel1_start=" << sett.channel1_start << ", impulses1_start=" << sett.impulses1_start);
        
        return true;

    } else {    
        // Конфигурация не была сохранена в EEPROM, инициализируем с нуля
        LOG_NOTICE(FPSTR(S_CFG), "crc failed=" << sett.crc );
        
        // Заполняем нулями всю конфигурацию
        memset(&sett, 0, sizeof(sett));

        sett.version = CURRENT_VERSION;  //для совместимости в будущем
        LOG_NOTICE(FPSTR(S_CFG), "version=" << sett.version);

        strncpy0(sett.waterius_host, WATERIUS_DEFAULT_DOMAIN, WATERIUS_HOST_LEN);

        strncpy0(sett.blynk_host, BLYNK_DEFAULT_DOMAIN, BLYNK_HOST_LEN);

        String email_title = "Новые показания {DEVICE_NAME}";
        strncpy0(sett.blynk_email_title, email_title.c_str(), BLYNK_EMAIL_TITLE_LEN);

        String email_template = "Показания:<br>Холодная: {V1}м³<br>Горячая: {V0}м³<hr>Расход за сутки:<br>Холодная: {V4}л<br>Горячая: {V3}л<hr>Напряжение: {V2}В<br>Перезагрузок: {V5}";
        strncpy0(sett.blynk_email_template, email_template.c_str(), BLYNK_EMAIL_TEMPLATE_LEN);

        //strncpy0(sett.mqtt_host, MQTT_DEFAULT_HOST, MQTT_HOST_LEN);
        String defaultTopic = String(MQTT_DEFAULT_TOPIC_PREFIX) + String(ESP.getChipId()) + "/";

        strncpy0(sett.mqtt_topic, defaultTopic.c_str(), MQTT_TOPIC_LEN);
        sett.mqtt_port = MQTT_DEFAULT_PORT;

        sett.liters_per_impuls = LITRES_PER_IMPULS_DEFAULT;

//Можно задать константы при компиляции, чтобы Вотериус сразу заработал

#ifdef BLYNK_KEY    
        #pragma message(VAR_NAME_VALUE(BLYNK_KEY))
        String key = VALUE(BLYNK_KEY);
        strncpy0(sett.blynk_key, key.c_str(), BLYNK_KEY_LEN);
        LOG_NOTICE(FPSTR(S_CFG), "default Blynk key=" << key);
#endif

#ifdef WATERIUS_HOST
        #pragma message(VAR_NAME_VALUE(WATERIUS_HOST))
        String waterius_host = VALUE(WATERIUS_HOST);
        strncpy0(sett.waterius_host, waterius_host.c_str(), WATERIUS_HOST_LEN);
        LOG_NOTICE(FPSTR(S_CFG), "default waterius_host=" << waterius_host);
#endif

#ifdef WATERIUS_EMAIL
        #pragma message(VAR_NAME_VALUE(WATERIUS_EMAIL))
        strncpy0(sett.waterius_email, VALUE(WATERIUS_EMAIL), EMAIL_LEN);
        LOG_NOTICE(FPSTR(S_CFG), "default waterius email=" << VALUE(WATERIUS_EMAIL));
#endif

#ifdef WATERIUS_KEY
        #pragma message(VAR_NAME_VALUE(WATERIUS_KEY))
        strncpy0(sett.waterius_key, VALUE(WATERIUS_KEY), WATERIUS_KEY_LEN);
        LOG_NOTICE(FPSTR(S_CFG), "default waterius key=" << VALUE(WATERIUS_KEY));
#else
        LOG_NOTICE(FPSTR(S_CFG), "Generate waterius key");
        WateriusHttps::generateSha256Token(sett.waterius_key, WATERIUS_KEY_LEN, 
                                           sett.waterius_email);
#endif

#if defined(SSID_NAME) 
#if defined(SSID_PASS)
        #pragma message(VAR_NAME_VALUE(SSID_NAME))
        #pragma message(VAR_NAME_VALUE(SSID_PASS))
        WiFi.begin(VALUE(SSID_NAME), VALUE(SSID_PASS), 0, NULL, false);  //connect=false, т.к. мы следом вызываем Wifi.begin
        LOG_NOTICE(FPSTR(S_CFG), "default ssid=" << VALUE(SSID_NAME) << ", pwd=" << VALUE(SSID_PASS));
        
        sett.crc = FAKE_CRC; //чтобы больше не попадать сюда
        return true;
#endif
#endif
        return false;
    }
}
         