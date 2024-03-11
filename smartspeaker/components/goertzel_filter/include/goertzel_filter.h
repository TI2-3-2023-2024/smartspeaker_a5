/**
 * Goertzel filter
 * 
 * Initial implementation by P.S.M. Goossens
 * Based on various sources, such as
 * https://www.embedded.com/the-goertzel-algorithm/
 * https://github.com/jacobrosenthal/Goertzel
 * https://www.st.com/content/ccc/resource/technical/document/design_tip/group0/20/06/95/0b/c3/8d/4a/7b/DM00446805/files/DM00446805.pdf/jcr:content/translations/en.DM00446805.pdf
 * 
 * Adapted by Hans van der Linden
 */
#ifndef GOERTZEL_FILTER_H
#define GOERTZEL_FILTER_H

#include <stdbool.h>
#include "esp_err.h"

struct goertzel_filt_dat_t;

/**
 * @brief Structure containing Goertzel filter configuration data
 */
typedef struct goertzel_filt_conf_t {
    int sample_rate;        // Number of samples per second [Hz]
    int target_freq;        // Target frequency to detect [Hz]
    int buffer_length;      // Number of samples to process [N]
    //float scaling_factor;   // Used in alternative magnitude calculation
} goertzel_filter_cfg_t;

/**
 * @brief Structure containing Goertzel filter data for processing
 */
typedef struct goertzel_filt_dat_t {
    int buffer_length;      // Number of samples to process [N]
    int sample_counter;     // Number of samples handled
    //float scaling_factor;   // Used in alternative magnitude calculation
    //float omega;
    float coefficient;      // Precalculated coefficient
    float q0;
    float q1;
    float q2;
    float magnitude;        // Calculated magnitude value
    bool updated;           // True whenever new magnitude is calculated
} goertzel_filter_data_t;

esp_err_t goertzel_filter_setup(goertzel_filter_data_t *data, goertzel_filter_cfg_t *config);
esp_err_t goertzel_filter_clear(goertzel_filter_data_t *data);
esp_err_t goertzel_filter_process(goertzel_filter_data_t *filter, int16_t *samples, int numSamples);
bool goertzel_filter_new_magnitude(goertzel_filter_data_t *filter, float *magnitude);

#endif