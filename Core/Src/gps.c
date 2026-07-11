#include "gps.h"
#include "sys_app.h"
#include <stdlib.h>
#include <string.h>



gps_sentence_t gps_get_sentence_type(uint8_t *gps_buffer){
    if (strncmp((char *)gps_buffer, "$GNRMC", 6) == 0) return GPS_SENTENCE_RMC;
    if (strncmp((char *)gps_buffer, "$GNGGA", 6) == 0) return GPS_SENTENCE_GGA;
    return GPS_SENTENCE_UNKNOWN;
}

// Big insight from Claude:
// splits on ',' without collapsing empty fields (unlike strtok_r), so a
// missing NMEA field (e.g. no fix -> ",,") still consumes its slot instead
// of shifting every later field left. Returns "" for an empty field, or
// NULL once the buffer is exhausted.
static char *gps_next_field(char **rest){
    if(*rest == NULL) return NULL;
    char *start = *rest;
    char *comma = strchr(start, ',');
    if(comma != NULL){
        *comma = '\0';
        *rest = comma + 1;
    } else {
        *rest = NULL;
    }
    return start;
}

static int gps_field_present(const char *token){
    return token != NULL && token[0] != '\0';
}

// ddmm.mmmm -> decimal degrees * 1e7 (double keeps full precision)
static int32_t gps_nmea_to_deg_e7(const char *token){
    double nmea = strtod(token, NULL);
    int32_t deg = (int32_t)(nmea / 100.0);
    double minutes = nmea - (double)deg * 100.0;
    return (int32_t)(((double)deg + minutes / 60.0) * 1e7);
}

// hhmmss.ss -> hour/minute/second
static void gps_parse_time(gps_t *gps_data, const char *token){
    uint32_t t = (uint32_t)strtod(token, NULL);
    gps_data->hour   = (uint8_t)(t / 10000);
    gps_data->minute = (uint8_t)((t / 100) % 100);
    gps_data->second = (uint8_t)(t % 100);
}

// ddmmyy -> day/month/year
static void gps_parse_date(gps_t *gps_data, const char *token){
    long d = strtol(token, NULL, 10);
    gps_data->day   = (uint8_t)(d / 10000);
    gps_data->month = (uint8_t)((d / 100) % 100);
    gps_data->year  = (uint8_t)(d % 100);
}



//Parse first token (header), then call the appropriate parser for the rest of the sentence
//store header in struct if it's a known sentence type, otherwise return without parsing
void gps_parser(gps_t *gps_data, uint8_t *gps_buffer){

    const char s[2] = ",";
    char *token;
    char* rest = (char *)gps_buffer;

    token = strtok_r(rest, s, &rest);
    if(token == NULL) return;
    gps_sentence_t sentence_type = gps_get_sentence_type((uint8_t *)token);
    if(sentence_type == GPS_SENTENCE_UNKNOWN) return;
    else{
        strncpy((char *)gps_data->header, token, sizeof(gps_data->header) - 1);
        gps_data->header[sizeof(gps_data->header) - 1] = '\0';
        gps_parse_sentence(gps_data, (uint8_t *)rest, sentence_type);
    }
    
}

void gps_parser_rmc(gps_t *gps_data, uint8_t *gps_buffer){
    char *token;
    char* rest = (char *)gps_buffer;

    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_parse_time(gps_data, token);
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->status = (uint8_t)token[0];
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->lat = gps_nmea_to_deg_e7(token);
    token = gps_next_field(&rest);
    if(gps_field_present(token)){
        gps_data->lat_ns = token[0];
        if(token[0] == 'S') gps_data->lat = -gps_data->lat;
    }
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->lon = gps_nmea_to_deg_e7(token);
    token = gps_next_field(&rest);
    if(gps_field_present(token)){
        gps_data->lon_ew = token[0];
        if(token[0] == 'W') gps_data->lon = -gps_data->lon;
    }
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->speed = (uint16_t)(strtod(token, NULL) * 100.0);
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->course = (uint16_t)(strtod(token, NULL) * 100.0);
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_parse_date(gps_data, token);

}

void gps_parser_gga(gps_t *gps_data, uint8_t *gps_buffer){
    char *token;
    char* rest = (char *)gps_buffer;

    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_parse_time(gps_data, token);
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->lat = gps_nmea_to_deg_e7(token);
    token = gps_next_field(&rest);
    if(gps_field_present(token)){
        gps_data->lat_ns = token[0];
        if(token[0] == 'S') gps_data->lat = -gps_data->lat;
    }
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->lon = gps_nmea_to_deg_e7(token);
    token = gps_next_field(&rest);
    if(gps_field_present(token)){
        gps_data->lon_ew = token[0];
        if(token[0] == 'W') gps_data->lon = -gps_data->lon;
    }
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->fix_quality = (uint8_t)strtol(token, NULL, 10);
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->num_sats = (uint8_t)strtol(token, NULL, 10);
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->hdop = (uint16_t)(strtod(token, NULL) * 100.0);
    token = gps_next_field(&rest);
    if(gps_field_present(token)) gps_data->altitude = (int32_t)(strtod(token, NULL) * 1000.0);

}


void gps_parse_sentence(gps_t *gps_data, uint8_t *gps_buffer, gps_sentence_t sentence_type){
    switch (sentence_type)
    {
        case GPS_SENTENCE_RMC: gps_parser_rmc(gps_data, gps_buffer); break;
        case GPS_SENTENCE_GGA: gps_parser_gga(gps_data, gps_buffer); break;
        default: break;
    }
}


// FOR CONFIGURING THE GPS MODULE (UBX PROTOCOL) AI Generated
// I was just so tired, Ma..
static uint8_t ubx_valset_value_size(uint32_t key)
{
    switch ((key >> 28) & 0x07) {
        case 0x01: return 1;   // L  (1 bit, stored as 1 byte)
        case 0x02: return 1;   // U1 / I1
        case 0x03: return 2;   // U2 / I2
        case 0x04: return 4;   // U4 / I4
        case 0x05: return 8;   // U8 / I8
        default:   return 0;
    }
}

void gps_set_baud(UART_HandleTypeDef *huart, uint32_t new_baud, uint8_t *gps_rx_byte)
{
    gps_valset(huart, CFG_UART1_BAUDRATE, new_baud, UBX_LAYER_RAM);
    HAL_Delay(100);

    HAL_UART_DeInit(huart);
    huart->Init.BaudRate = new_baud;
    HAL_UART_Init(huart);

    HAL_UART_Receive_IT(huart, gps_rx_byte, 1);   // re-arm your byte/buffer RX
}

void gps_valset(UART_HandleTypeDef *huart, uint32_t key, uint64_t value, uint8_t layers)
{
    uint8_t val_size = ubx_valset_value_size(key);
    uint16_t payload_len = 4 + 4 + val_size;   // cfgHeader(4) + key(4) + value(N)

    uint8_t frame[6 + 4 + 4 + 8 + 2];          // max: envelope + header + key + 8B value + ck
    uint16_t i = 0;

    frame[i++] = 0xB5;
    frame[i++] = 0x62;
    frame[i++] = 0x06;                          // class CFG
    frame[i++] = 0x8A;                          // id VALSET
    frame[i++] = (uint8_t)(payload_len & 0xFF);
    frame[i++] = (uint8_t)(payload_len >> 8);

    frame[i++] = 0x00;                          // version
    frame[i++] = layers;                        // layer bitmask
    frame[i++] = 0x00;                          // reserved
    frame[i++] = 0x00;                          // reserved

    frame[i++] = (uint8_t)(key & 0xFF);         // key, little-endian
    frame[i++] = (uint8_t)((key >> 8) & 0xFF);
    frame[i++] = (uint8_t)((key >> 16) & 0xFF);
    frame[i++] = (uint8_t)((key >> 24) & 0xFF);

    for (uint8_t b = 0; b < val_size; b++) {    // value, little-endian
        frame[i++] = (uint8_t)((value >> (8 * b)) & 0xFF);
    }

    uint8_t ck_a = 0, ck_b = 0;                 // checksum over class..end of payload
    for (uint16_t j = 2; j < i; j++) {
        ck_a += frame[j];
        ck_b += ck_a;
    }
    frame[i++] = ck_a;
    frame[i++] = ck_b;

    HAL_UART_Transmit(huart, frame, i, 100);
}

void gps_config(){

// asks the GPS module to only output RMC and GGA sentences, which contain the most relevant info for pebble
  gps_valset(&hlpuart1, CFG_MSGOUT_NMEA_GLL_UART1, 0, UBX_LAYER_RAM);
  gps_valset(&hlpuart1, CFG_MSGOUT_NMEA_GSA_UART1, 0, UBX_LAYER_RAM);
  gps_valset(&hlpuart1, CFG_MSGOUT_NMEA_GSV_UART1, 0, UBX_LAYER_RAM);
  gps_valset(&hlpuart1, CFG_MSGOUT_NMEA_VTG_UART1, 0, UBX_LAYER_RAM);

  // configure  gps module rates
  gps_valset(&hlpuart1, CFG_MSGOUT_NMEA_GGA_UART1, 1, UBX_LAYER_RAM);
  gps_valset(&hlpuart1, CFG_MSGOUT_NMEA_RMC_UART1, 1, UBX_LAYER_RAM);

  // start continuous circular DMA reception of the NMEA stream
  HAL_UART_Receive_DMA(&hlpuart1, dma_rx_buf, DMA_RX_BUF_SIZE);
}