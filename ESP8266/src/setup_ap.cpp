
#include "setup_ap.h"
#include "Logging.h"
#include "wifi_settings.h"

#include <ESP8266WiFi.h>
#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiClient.h>
#include <EEPROM.h>
#include "utils.h"
#include "WateriusHttps.h"
#include "master_i2c.h"

#include "strings_ru.h"

#define AP_NAME "Waterius_" FIRMWARE_VERSION

extern SlaveData data;
extern MasterI2C masterI2C;

SlaveData runtime_data;

void LOG_HEAP()
{
    uint32_t free;
    uint16_t max;
    uint8_t frag;
    ESP.getHeapStats(&free, &max, &frag);
    LOG_NOTICE(FPSTR(S_ESP), "[MEM] free: " << free << " | max: " << max << " | frag: " << frag << "%%");
}

#define IMPULS_LIMIT_1 3  // Если пришло импульсов меньше 3, то перед нами 10л/имп. Если больше, то 1л/имп.

uint8_t get_factor() {
    return (runtime_data.impulses1 - data.impulses1 <= IMPULS_LIMIT_1) ? 10 : 1;
}

void update_data(String &message) {
    if (masterI2C.getSlaveData(runtime_data)) {
        String state0good(FPSTR(S_STATE_NULL));
        String state0bad(FPSTR(S_STATE_BAD));
        String state1good(FPSTR(S_STATE_NULL));
        String state1bad(FPSTR(S_STATE_BAD));
        uint32_t delta0 = runtime_data.impulses0 - data.impulses0;
        uint32_t delta1 = runtime_data.impulses1 - data.impulses1;
        
        if (delta0 > 0) {
            state0good = FPSTR(S_STATE_CONNECTED);
            state0bad = FPSTR(S_STATE_NULL);
        }
        if (delta1 > 0) {
            state1good = FPSTR(S_STATE_CONNECTED);
            state1bad = FPSTR(S_STATE_NULL);
        }

        message = "{\"state0good\": ";  //TODO F()
        message += state0good;
        message += ", \"state0bad\": ";
        message += state0bad;
        message += ", \"state1good\": ";
        message += state1good;
        message += ", \"state1bad\": ";
        message += state1bad;
        message += ", \"factor\": ";
        message += String(get_factor());
        message += " }";
    }
    else {
        message = "{\"error\": \"Ошибка\"}";
    }
}

WiFiManager wm;

void appendScanItemOut(String &page) {

    if(!wm._numNetworks) 
        wm.WiFi_scanNetworks(); // scan in case this gets called before any scans

    int n = wm._numNetworks;
    if (n == 0) {
      LOG_ERROR(FPSTR(S_AP), F("No networks found"));
      page += FPSTR(S_NO_NETWORKS); // @token nonetworks
    } 
    else {
        LOG_ERROR(FPSTR(S_AP), F("networks found"));
        //sort networks
        int indices[n];
        for (int i = 0; i < n; i++) {
            indices[i] = i;
        }

        // RSSI SORT
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
            if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
                std::swap(indices[i], indices[j]);
            }
            }
        }
        // remove duplicates ( must be RSSI sorted )
        if (wm._removeDuplicateAPs) {
            String cssid;
            for (int i = 0; i < n; i++) {
                if (indices[i] == -1) continue;
                cssid = WiFi.SSID(indices[i]);
                for (int j = i + 1; j < n; j++) {
                    if (cssid == WiFi.SSID(indices[j])) {
                        LOG_ERROR(FPSTR(S_AP), F("DUP AP:") << WiFi.SSID(indices[j]));
                        indices[j] = -1; // set dup aps to index -1
                    }
                }
            }
        }

      // token precheck, to speed up replacements on large ap lists
      String HTTP_ITEM_STR = FPSTR(ITEM);

      // toggle icons with percentage
      HTTP_ITEM_STR.replace("{h}", wm._scanDispOptions ? "" : "h");
      HTTP_ITEM_STR.replace("{qi}", FPSTR(ITEM_QI));
      HTTP_ITEM_STR.replace("{h}", wm._scanDispOptions ? "h" : "");
 
      // set token precheck flags
      bool tok_r = HTTP_ITEM_STR.indexOf(FPSTR(T_r)) > 0;
      bool tok_R = HTTP_ITEM_STR.indexOf(FPSTR(T_R)) > 0;
      bool tok_e = HTTP_ITEM_STR.indexOf(FPSTR(T_e)) > 0;
      bool tok_q = HTTP_ITEM_STR.indexOf(FPSTR(T_q)) > 0;
      bool tok_i = HTTP_ITEM_STR.indexOf(FPSTR(T_i)) > 0;
      
      //display networks in page
      for (int i = 0; i < n; i++) {
        if (indices[i] == -1) continue; // skip dups

        LOG_ERROR(FPSTR(S_AP), F("AP: ") << (String)WiFi.RSSI(indices[i]) << " " << (String)WiFi.SSID(indices[i]));

        int rssiperc = wm.getRSSIasQuality(WiFi.RSSI(indices[i]));
        uint8_t enc_type = WiFi.encryptionType(indices[i]);

        if (wm._minimumQuality == -1 || wm._minimumQuality < rssiperc) {
            String item = HTTP_ITEM_STR;
            item.replace(FPSTR(T_v), wm.htmlEntities(WiFi.SSID(indices[i]))); // ssid no encoding
            if(tok_e) item.replace(FPSTR(T_e), wm.encryptionTypeStr(enc_type));
            if(tok_r) item.replace(FPSTR(T_r), (String)rssiperc); // rssi percentage 0-100
            if(tok_R) item.replace(FPSTR(T_R), (String)WiFi.RSSI(indices[i])); // rssi db
            if(tok_q) item.replace(FPSTR(T_q), (String)int(round(map(rssiperc,0,100,1,4)))); //quality icon 1-4
            if(tok_i){
                if (enc_type != WM_WIFIOPEN) {
                    item.replace(FPSTR(T_i), F("l"));
                } else {
                    item.replace(FPSTR(T_i), "");
                }
            }
            //DEBUG_WM(item);
            page += item;
            delay(0);
        }

      }
      page += FPSTR(HTTP_BR);
    }
}

void appendParamOut(String &page){

  if(wm._paramsCount > 0){

    String HTTP_PARAM_temp = FPSTR(HTTP_FORM_LABEL);
    HTTP_PARAM_temp += FPSTR(HTTP_FORM_PARAM);
    bool tok_I = HTTP_PARAM_temp.indexOf(FPSTR(T_I)) > 0;
    bool tok_i = HTTP_PARAM_temp.indexOf(FPSTR(T_i)) > 0;
    bool tok_n = HTTP_PARAM_temp.indexOf(FPSTR(T_n)) > 0;
    bool tok_p = HTTP_PARAM_temp.indexOf(FPSTR(T_p)) > 0;
    bool tok_t = HTTP_PARAM_temp.indexOf(FPSTR(T_t)) > 0;
    bool tok_l = HTTP_PARAM_temp.indexOf(FPSTR(T_l)) > 0;
    bool tok_v = HTTP_PARAM_temp.indexOf(FPSTR(T_v)) > 0;
    bool tok_c = HTTP_PARAM_temp.indexOf(FPSTR(T_c)) > 0;

    char valLength[5];
    // add the extra parameters to the form
    for (int i = 0; i < wm._paramsCount; i++) {
      if (wm._params[i] == NULL || wm._params[i]->getValueLength() == 0) {
        LOG_ERROR(FPSTR(S_AP), F("WiFiManagerParameter is out of scope"));
        break;
      }

     // label before or after, @todo this could be done via floats or CSS and eliminated
     String pitem;
      switch (wm._params[i]->getLabelPlacement()) {
        case WFM_LABEL_BEFORE:
          pitem = FPSTR(FORM_LABEL);
          pitem += FPSTR(FORM_PARAM);
          break;
        case WFM_LABEL_AFTER:
          pitem = FPSTR(FORM_PARAM);
          pitem += FPSTR(FORM_LABEL);
          break;
        default:
          // WFM_NO_LABEL
          pitem = FPSTR(FORM_PARAM);
          break;
      }

      // Input templating
      // "<br/><input id='{i}' name='{n}' maxlength='{l}' value='{v}' {c}>";
      // if no ID use customhtml for item, else generate from param string
      if (wm._params[i]->getID() != NULL) {
        if(tok_I)pitem.replace(FPSTR(T_I), (String)FPSTR(S_parampre)+(String)i); // T_I id number
        if(tok_i)pitem.replace(FPSTR(T_i), wm._params[i]->getID()); // T_i id name
        if(tok_n)pitem.replace(FPSTR(T_n), wm._params[i]->getID()); // T_n id name alias
        if(tok_p)pitem.replace(FPSTR(T_p), FPSTR(T_t)); // T_p replace legacy placeholder token
        if(tok_t)pitem.replace(FPSTR(T_t), wm._params[i]->getLabel()); // T_t title/label
        snprintf(valLength, 5, "%d", wm._params[i]->getValueLength());
        if(tok_l)pitem.replace(FPSTR(T_l), valLength); // T_l value length
        if(tok_v)pitem.replace(FPSTR(T_v), wm._params[i]->getValue()); // T_v value
        if(tok_c)pitem.replace(FPSTR(T_c), wm._params[i]->getCustomHTML()); // T_c meant for additional attributes, not html, but can stuff
      } else {
        pitem = wm._params[i]->getCustomHTML();
      }

      page += pitem;
    }
  }
}

void handleRoot() {
    LOG_NOTICE(FPSTR(S_AP), F("<- HTTP Root"));

    if (wm.captivePortal()) 
        return; // If captive portal redirect instead of displaying the page
    wm.handleRequest();

    String page; page.reserve(5000);
    page += FPSTR(HTTP_HEAD_START);
    page.replace(FPSTR(T_v), F("Настройка Ватериуса"));
    page += FPSTR(ROOT_STYLE);
    page += FPSTR(HEAD_END);
    
    page += FPSTR(DIV_LOGO);
    page += FPSTR(ROOT_MAIN);
    page += FPSTR(F("<form action='/wifi'    method='get'><button class='button'>Настроить Ватериус</button></form><br/>\n"));
    
    page += FPSTR(HTTP_END);

    wm.server->send(200, FPSTR(HTTP_HEAD_CT), page);
}

void handleWifi() {
    LOG_NOTICE(FPSTR(S_AP), F("<- HTTP Wifi"));
    LOG_HEAP();

    wm.handleRequest();
    {
        String page; page.reserve(12000);
        LOG_HEAP();

        page += FPSTR(HTTP_HEAD_START);
        page.replace(FPSTR(T_v), F("Настройка Ватериуса"));
        page += FPSTR(CONF_SCRIPT);
        page += FPSTR(CONF_STYLE);
        page += FPSTR(HEAD_END);
        page += FPSTR(DIV_LOGO);
        page += FPSTR(WIFI_PAGE_TEXT);
        
        wm.WiFi_scanNetworks(wm.server->hasArg(F("refresh")), false); //wifiscan, force if arg refresh
        appendScanItemOut(page);

        String pitem = FPSTR(HTTP_FORM_START);
        pitem.replace(FPSTR(T_v), F("wifisave")); // set form action
        page += pitem;

        pitem = FPSTR(FORM_WIFI);
        pitem.replace(FPSTR(T_v), wm.WiFi_SSID());
        page += pitem;

        page += FPSTR(HTTP_FORM_PARAM_HEAD);
        appendParamOut(page);

        page += FPSTR(FORM_END);
        page += FPSTR(HTTP_END);

        LOG_NOTICE(FPSTR(S_AP), F("Page len=") << page.length());
        wm.server->send(200, FPSTR(HTTP_HEAD_CT), page);
    }

    LOG_NOTICE(FPSTR(S_AP), F("Sent config page"));
    LOG_HEAP();
    
}

void handleWifiSave() {
    LOG_NOTICE(FPSTR(S_AP), F("<- HTTP WiFi save "));
    LOG_HEAP();

    wm.handleRequest();

    //SAVE/connect here
    wm._ssid = wm.server->arg(F("s")).c_str();
    wm._pass = wm.server->arg(F("p")).c_str();

    wm.doParamSave();

    {
        String page; page.reserve(5000);
        page += FPSTR(HTTP_HEAD_START);
        page.replace(FPSTR(T_v), F("Попытка подключения"));
        page += FPSTR(END_STYLE);
        page += FPSTR(HEAD_END);

        page += FPSTR(HTTP_SAVED_TEXT);

        page += FPSTR(HTTP_END);
        
        LOG_HEAP();
        wm.server->sendHeader(FPSTR(HTTP_HEAD_CL), String(page.length()));
        wm.server->sendHeader(FPSTR(HTTP_HEAD_CORS), FPSTR(HTTP_HEAD_CORS_ALLOW_ALL));
        wm.server->send(200, FPSTR(HTTP_HEAD_CT), page);
    }

    LOG_NOTICE(FPSTR(S_AP), F("Sent wifi save page"));
    LOG_HEAP();
    wm.connect = true; //signal ready to connect/reset process in processConfigPortal
}

void handleStates() {
    String message;
    message.reserve(200);
    update_data(message);
    wm.server->send(200, FPSTR(HTTP_TEXT_PLAIN), message);
}

void bindServerCallback() {
    wm.server->on(String(FPSTR(R_root)).c_str(),      handleRoot);
    wm.server->on(String(FPSTR(R_wifi)).c_str(),      handleWifi);
    wm.server->on(String(FPSTR(R_wifisave)).c_str(),  handleWifiSave);
    wm.server->on(String(FPSTR(R_states)).c_str(),    handleStates);
}


void setup_ap(Settings &sett, const SlaveData &data, const CalculatedData &cdata) 
{
    wm.debugPlatformInfo();
    wm.setWebServerCallback(bindServerCallback);

    LOG_NOTICE(FPSTR(S_AP), FPSTR(S_CAPTIVE_PORTAL));
    
    // Настройки HTTP 

    WiFiManagerParameter param_waterius_email( "wmail", "Электронная почта с сайта waterius.ru",  sett.waterius_email, EMAIL_LEN-1);
    wm.addParameter( &param_waterius_email);

    // Чекбокс доп. настроек

    WiFiManagerParameter checkbox("<br><br><br><label class='cnt'>Дополнительные настройки<input type='checkbox' id='chbox' name='chbox' onclick='showMe()'><span class='mrk'></span></label>");
    wm.addParameter(&checkbox);

    WiFiManagerParameter div_start("<div id='advanced' style='display:none'>");
    wm.addParameter(&div_start);
    
    // Сервер http запроса 

    WiFiManagerParameter param_waterius_host( "whost", "Адрес сервера (включает отправку)",  sett.waterius_host, WATERIUS_HOST_LEN-1);
    wm.addParameter( &param_waterius_host );


    // Настройки Blynk.сс

    WiFiManagerParameter label_blynk("<h3>Blynk.cc</h3>");
    wm.addParameter( &label_blynk);
    WiFiManagerParameter param_blynk_host( "bhost", "Адрес сервера",  sett.blynk_host, BLYNK_HOST_LEN-1);
    wm.addParameter( &param_blynk_host );
    WiFiManagerParameter param_blynk_key( "bkey", "Уникальный ключ (включает отправку)",  sett.blynk_key, BLYNK_KEY_LEN-1);
    wm.addParameter( &param_blynk_key );
    WiFiManagerParameter param_blynk_email( "bemail", "Адрес эл. почты (включает ежедневные письма)",  sett.blynk_email, EMAIL_LEN-1);
    wm.addParameter( &param_blynk_email );
    WiFiManagerParameter param_blynk_email_title( "btitle", "Тема письма",  sett.blynk_email_title, BLYNK_EMAIL_TITLE_LEN-1);
    wm.addParameter( &param_blynk_email_title );
    WiFiManagerParameter param_blynk_email_template( "btemplate", "Текст письма",  sett.blynk_email_template, BLYNK_EMAIL_TEMPLATE_LEN-1);
    wm.addParameter( &param_blynk_email_template );

    // Настройки MQTT
    
    WiFiManagerParameter label_mqtt("<h3>MQTT</h3>");
    wm.addParameter( &label_mqtt);
    WiFiManagerParameter param_mqtt_host( "mhost", "Адрес сервера (включает отправку)<br/>Пример: broker.hivemq.com",  sett.mqtt_host, MQTT_HOST_LEN-1);
    wm.addParameter( &param_mqtt_host );
    LongParameter param_mqtt_port( "mport", "Порт",  sett.mqtt_port);
    wm.addParameter( &param_mqtt_port );
    WiFiManagerParameter param_mqtt_login( "mlogin", "Логин",  sett.mqtt_login, MQTT_LOGIN_LEN-1);
    wm.addParameter( &param_mqtt_login );
    WiFiManagerParameter param_mqtt_password( "mpassword", "Пароль",  sett.mqtt_password, MQTT_PASSWORD_LEN-1);
    wm.addParameter( &param_mqtt_password );
    WiFiManagerParameter param_mqtt_topic( "mtopic", "Topic",  sett.mqtt_topic, MQTT_TOPIC_LEN-1);
    wm.addParameter( &param_mqtt_topic );
    
    // конец доп. настроек
    WiFiManagerParameter div_end("</div>");
    wm.addParameter(&div_end);
    
    // Счетчиков
    WiFiManagerParameter cold_water("<h3>Холодная вода</h3>");
    wm.addParameter(&cold_water);
            
    WiFiManagerParameter label_cold_info("<p>Спустите унитаз 1&ndash;3 раза (или вылейте не&nbsp;меньше 4&nbsp;л.), пока надпись не&nbsp;сменится на&nbsp;&laquo;подключен&raquo;. Если статус &laquo;не&nbsp;подключен&raquo;, проверьте провод в&nbsp;разъёме. Ватериус так определяет тип счётчика</p>");
    wm.addParameter( &label_cold_info);

    WiFiManagerParameter label_cold_state("<b><p class='bad' id='state1bad'></p><p class='good' id='state1good'></p></b>");
    wm.addParameter( &label_cold_state);

    WiFiManagerParameter label_cold("<label class='cold label'>Показания холодной воды</label>");
    wm.addParameter( &label_cold);
    FloatParameter param_channel1_start( "ch1", "",  cdata.channel1);
    wm.addParameter( &param_channel1_start);

    WiFiManagerParameter hot_water("<h3>Горячая вода</h3>");
    wm.addParameter(&hot_water);
            
    WiFiManagerParameter label_hot_info("<p>Откройте кран горячей воды, пока надпись не&nbsp;сменится на&nbsp;&laquo;подключен&raquo;</p>");
    wm.addParameter( &label_hot_info);
    
    WiFiManagerParameter label_hot_state("<b><p class='bad' id='state0bad'></p><p class='good' id='state0good'></p></b>");
    wm.addParameter( &label_hot_state );

    WiFiManagerParameter label_hot("<label class='hot label'>Показания горячей воды</label>");
    wm.addParameter( &label_hot);
    FloatParameter param_channel0_start( "ch0", "",  cdata.channel0);
    wm.addParameter( &param_channel0_start);

    wm.setConfigPortalTimeout(300);
    wm.setConnectTimeout(ESP_CONNECT_TIMEOUT);
    
    LOG_NOTICE(FPSTR(S_AP), FPSTR(S_START_CONFIG_PORTAL));

    // Запуск веб сервера на 192.168.4.1
    wm.startConfigPortal( AP_NAME );

    // Успешно подключились к Wi-Fi, можно засыпать
    LOG_NOTICE(FPSTR(S_AP), FPSTR(S_CONNECTED_TO_WIFI));

    // Переписываем введенные пользователем значения в Конфигурацию

    strncpy0(sett.waterius_email, param_waterius_email.getValue(), EMAIL_LEN);
    strncpy0(sett.waterius_host, param_waterius_host.getValue(), WATERIUS_HOST_LEN);

    // Генерируем ключ используя и введенную эл. почту
    if (strnlen(sett.waterius_key, WATERIUS_KEY_LEN) == 0) {
        LOG_NOTICE(FPSTR(S_CFG), FPSTR(S_GENERATE_WATERIUS_KEY));
        WateriusHttps::generateSha256Token(sett.waterius_key, WATERIUS_KEY_LEN, 
                                           sett.waterius_email);
    }

    strncpy0(sett.blynk_key, param_blynk_key.getValue(), BLYNK_KEY_LEN);
    strncpy0(sett.blynk_host, param_blynk_host.getValue(), BLYNK_HOST_LEN);
    strncpy0(sett.blynk_email, param_blynk_email.getValue(), EMAIL_LEN);
    strncpy0(sett.blynk_email_title, param_blynk_email_title.getValue(), BLYNK_EMAIL_TITLE_LEN);
    strncpy0(sett.blynk_email_template, param_blynk_email_template.getValue(), BLYNK_EMAIL_TEMPLATE_LEN);

    strncpy0(sett.mqtt_host, param_mqtt_host.getValue(), MQTT_HOST_LEN);
    strncpy0(sett.mqtt_login, param_mqtt_login.getValue(), MQTT_LOGIN_LEN);
    strncpy0(sett.mqtt_password, param_mqtt_password.getValue(), MQTT_PASSWORD_LEN);
    strncpy0(sett.mqtt_topic, param_mqtt_topic.getValue(), MQTT_TOPIC_LEN);
    sett.mqtt_port = param_mqtt_port.getValue();    

    // Текущие показания счетчиков
    sett.channel0_start = param_channel0_start.getValue();
    sett.channel1_start = param_channel1_start.getValue();

    sett.liters_per_impuls = get_factor(); //param_litres_per_imp.getValue();
    LOG_NOTICE(FPSTR(S_AP), "factor=" << sett.liters_per_impuls );

    // Запоминаем кол-во импульсов Attiny соответствующих текущим показаниям счетчиков
    sett.impulses0_start = data.impulses0;
    sett.impulses1_start = data.impulses1;

    // Предыдущие показания счетчиков. Вносим текущие значения.
    sett.impulses0_previous = sett.impulses0_start;
    sett.impulses1_previous = sett.impulses1_start;

    LOG_NOTICE(FPSTR(S_AP), "impulses0=" << sett.impulses0_start );
    LOG_NOTICE(FPSTR(S_AP), "impulses1=" << sett.impulses1_start );

    sett.crc = FAKE_CRC; // todo: сделать нормальный crc16
    storeConfig(sett);
}
