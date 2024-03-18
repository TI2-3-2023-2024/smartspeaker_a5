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
#include <math.h>

#include "goertzel_filter.h"

//static const char *TAG = "GOERTZEL_FILTER";


/**
 * Setup the filter using the provided config data to prepare for processing
 */
esp_err_t goertzel_filter_setup(goertzel_filter_data_t *data, goertzel_filter_cfg_t *config)
{
    // Calculate constants for the Goertzel filter
    float numSamples = (float) config->buffer_length;
    //data->scaling_factor = numSamples / 2.0f;
    data->buffer_length = config->buffer_length;
    int k = (int) (0.5f + ((numSamples * config->target_freq) / config->sample_rate));
    float omega = (2.0f * M_PI * k) / numSamples;
    //data->omega = omega;
    data->coefficient = 2.0f * cosf(omega);

    return goertzel_filter_clear(data);
}

/**
 * Clear the filter data to prepare for processing of samples
 */
esp_err_t goertzel_filter_clear(goertzel_filter_data_t *data)
{
    data->q0 = 0.0f;
    data->q1 = 0.0f;
    data->q2 = 0.0f;
    data->sample_counter = 0;   // Start a new block of samples
    data->magnitude = 0.0f;     // Start off with a magnitude of 0
    data->updated = false;      // No new magnitude yet

    return ESP_OK;
}

esp_err_t goertzel_filter_process(goertzel_filter_data_t *filter, int16_t *samples, int numSamples)
{
    for (int s = 0; s < numSamples; s++) {
        // Process one sample
        float sample = (float) samples[s];
        filter->q0 = sample + filter->coefficient * filter->q1 - filter->q2; // Goertzel filter equation
        filter->q2 = filter->q1;
        filter->q1 = filter->q0;
        filter->sample_counter += 1;
        // If an entire buffer length of samples is processed, calculate the magnitude
        if (filter->sample_counter >= filter->buffer_length) {
            // Magnitude calculation
            filter->magnitude = sqrtf(filter->q1 * filter->q1 + filter->q2 * filter->q2 - filter->q1 * filter->q2 * filter->coefficient);
            filter->updated = true;     // New magnitude value is available
            filter->sample_counter = 0; // Start a new block of samples
           
            // Alternative magnitude calculation (more difficult)
            //float real = (filter->q1 - filter->q2 * cosf(filter->omega)) / filter->scaling_factor;
            //float imag = (filter->q2 * sinf(filter->omega)) / filter->scaling_factor;
            //filter->magnitude = sqrtf(real*real + imag*imag);

            // Reset the filter data
            filter->q0 = 0.0f;
            filter->q1 = 0.0f;
            filter->q2 = 0.0f;
        }
    }
    return ESP_OK;
}

/**
 * Return true if a new magnitude value is available and update the value of the
 * magnitude parameter accordingly,
 * or return false if no new magnitude is available
 */
bool goertzel_filter_new_magnitude(goertzel_filter_data_t *filter, float *magnitude)
{
    if (filter->updated) {
        *magnitude = filter->magnitude;
        filter->updated = false;
        return true;
    } else {
        return false;
    }
}
