/*
 * Maxima main.cpp
 *
 *  Created on: 1 Jan. 2018
 *      Author: William
 */


#include <esp_log.h>
#include <esp_spiffs.h>
#include <driver/i2s.h>                       // Library of I2S routines, comes with ESP32 standard install
#include <cstring>
#include <errno.h>

extern "C" {
    void app_main();
}

static const char *TAG = "maxima_main";

// Speakers I2S
#define SPEAKER_BIT_CLOCK      GPIO_NUM_27  // The bit clock connection, goes to pin 27 of ESP32
#define SPEAKER_WORD_SELECT    GPIO_NUM_26  // Word select, also known as word select or left right clock
#define SPEAKER_DATA_OUT       GPIO_NUM_25  // Data out from the ESP32, connect to DIN on 38357A

static const i2s_port_t i2s_num = I2S_NUM_0;  // i2s port number

static const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,                            // Note, this will be changed later
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // ie Stereo
        .communication_format = (i2s_comm_format_t) I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,       // high interrupt priority
        .dma_buf_count = 8,                             // 8 buffers
        .dma_buf_len = 1024,                            // 1K per buffer, so 8K of buffer space
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = -1,
        .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,
        .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT
};

static const i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = SPEAKER_BIT_CLOCK,                  // The bit clock connection, goes to pin 27 of ESP32
        .ws_io_num = SPEAKER_WORD_SELECT,                 // Word select, also known as word select or left right clock
        .data_out_num = SPEAKER_DATA_OUT,                 // Data out from the ESP32, connect to DIN on 38357A
        .data_in_num = I2S_PIN_NO_CHANGE                  // we are not interested in I2S data into the ESP32
};

typedef struct {
    // Data Section
    char chunkID[4];            // The letters "data" (if it is a data section), otherwise LIST or similar)
    uint32_t chunk_size;        // Size of the data that follows
} wav_chunk_t;

typedef struct {
    //   RIFF Section
    char RIFFSectionID[4];      // Letters "RIFF"
    uint32_t Size;              // Size of entire file less 8
    char RiffFormat[4];         // Letters "WAVE"

    //   Format Section
    char FormatSectionID[4];    // letters "fmt"
    uint32_t FormatSize;        // Size of format section less 8
    uint16_t FormatID;          // 1=uncompressed PCM
    uint16_t NumChannels;       // 1=mono,2=stereo
    uint32_t SampleRate;        // 44100, 16000, 8000 etc.
    uint32_t ByteRate;          // =SampleRate * Channels * (BitsPerSample/8)
    uint16_t BlockAlign;        // =Channels * (BitsPerSample/8)
    uint16_t BitsPerSample;     // 8,16,24 or 32
    wav_chunk_t data;
} wav_header_t;

typedef struct {
    char* data;
    uint32_t sample_rate;
    uint32_t nr_bytes;
    char* filename;
} wav_data_t;

#define WAV_HEADER_SIZE sizeof(wav_header_t)
#define SILENCE_SIZE 8096
char* SILENCE;

// NB All WAV files must be stereo because that is the channel_format we provide in i2s_config
//#define FILE_ON_YOUR_MARKS  "/OYM-USA-male-1-16000.wav"
//#define START_TONE  "/StartTone-16000.wav"

#define FILE_ON_YOUR_MARKS  "/OYM-USA-male-1-Middle.wav"
#define FILE_ON_YOUR_MARKS_NO_MIDDLE  "/OYM-USA-male-1-NoMiddle.wav"
#define START_TONE  "/StartTone-Middle.wav"

static void init_sound() {

    // Configure SPIFFS for reading WAV file
    const esp_vfs_spiffs_conf_t conf = {
            .base_path = "",
            .partition_label = nullptr,
            .max_files = 5,
            .format_if_mount_failed = false
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    // Initialise i2s sound pins.
    ESP_ERROR_CHECK(i2s_driver_install(i2s_num, &i2s_config, 0, nullptr));   // Allocate resources to run I2S. NB not using an event queue TODO Try using an event queue!!!
    ESP_ERROR_CHECK(i2s_set_pin(i2s_num, &pin_config));                      // Tell it the pins you will be using
    //ESP_ERROR_CHECK(i2s_stop(i2s_num));

    //buzzer_evt_queue = xQueueCreate(5, sizeof(uint32_t));

    SILENCE = (char*) malloc(SILENCE_SIZE);
    memset(SILENCE, 0, SILENCE_SIZE);
/*
    xTaskCreate(
            &sound_buzzer_task,
            "soundTask",
            STACK_SIZE,
            (void*) nullptr, // params
            TASK_PRIORITY,
            nullptr
            );
*/
}


static bool validate_wav_data(wav_header_t* Wav) {

    if (memcmp(Wav->RIFFSectionID, "RIFF", 4) != 0) {
        ESP_LOGW(TAG, "Invalid data - Not RIFF format");
        return false;
    }
    if (memcmp(Wav->RiffFormat, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "Invalid data - Not Wave file");
        return false;
    }
    if (memcmp(Wav->FormatSectionID, "fmt", 3) != 0) {
        ESP_LOGW(TAG, "Invalid data - No format section found");
        return false;
    }
    if (memcmp(Wav->data.chunkID, "data", 4) != 0) {
        ESP_LOGW(TAG, "Invalid data - data section not found");
        return false;
    }
    if (Wav->FormatID != 1) {
        ESP_LOGW(TAG, "Invalid data - format Id must be 1");
        return false;
    }
    if (Wav->FormatSize!=16) {
        ESP_LOGW(TAG, "Invalid data - format section size must be 16.");
        return false;
    }
    if ((Wav->NumChannels != 1) && (Wav->NumChannels != 2)) {
        ESP_LOGW(TAG, "Invalid data - only mono or stereo permitted.");
        return false;
    }
    if (Wav->SampleRate > 48000) {
        ESP_LOGW(TAG, "Invalid data - Sample rate cannot be greater than 48000");
        return false;
    }
    if ((Wav->BitsPerSample != 8) && (Wav->BitsPerSample != 16)) {
        ESP_LOGW(TAG, "Invalid data - Only 8 or 16 bits per sample permitted.");
        return false;
    }
    return true;
}

static void log_wav_header(wav_header_t* Wav) {
    if (memcmp(Wav->RIFFSectionID, "RIFF", 4) != 0) {
        ESP_LOGE(TAG, "Not a RIFF format file - '%s'", Wav->RIFFSectionID);
        return;
    }
    if (memcmp(Wav->RiffFormat, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a WAVE file -  '%s'", Wav->RiffFormat);
        return;
    }
    if (memcmp(Wav->FormatSectionID, "fmt", 3) != 0) {
        ESP_LOGE(TAG, "fmt ID not present - '%s'", Wav->FormatSectionID);
        return;
    }
    if (memcmp(Wav->data.chunkID, "data", 4) != 0) {
        ESP_LOGE(TAG, "data ID not present - '%s'", Wav->data.chunkID);
        return;
    }
    // All looks good, dump the data
    ESP_LOGI(TAG, "Total size : %d", Wav->Size);
    ESP_LOGI(TAG, "Format section size : %d", Wav->FormatSize);
    ESP_LOGI(TAG, "Wave format : %d", Wav->FormatID);
    ESP_LOGI(TAG, "Channels : %d", Wav->NumChannels);
    ESP_LOGI(TAG, "Sample Rate : %d", Wav->SampleRate);
    ESP_LOGI(TAG, "Byte Rate : %d", Wav->ByteRate);
    ESP_LOGI(TAG, "Block Align : %d", Wav->BlockAlign);
    ESP_LOGI(TAG, "Bits Per Sample : %d", Wav->BitsPerSample);
    ESP_LOGI(TAG, "Data Size : %d", Wav->data.chunk_size);
}

/**
 * Loads the wav file and populates the pointer to the header and the pointer to the opened File.
 */
static esp_err_t load_wav_header(char* filename, wav_header_t* wav_header, FILE** f) {

    const int64_t start_ms = esp_timer_get_time() / 1000;

    // Use POSIX and C standard library functions to work with files.
    // Open the file for reading.
    ESP_LOGI(TAG, "Opening file");
    *f = fopen(filename, "r");
    if (*f == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for reading errno=%d err=str=%s", errno, strerror(errno));
        return ESP_FAIL;
    }

    // Read Wav header
    char buffer[WAV_HEADER_SIZE];
    fread(buffer, WAV_HEADER_SIZE, 1, *f);

    // Validate it.
    memcpy(wav_header, &buffer, WAV_HEADER_SIZE);  // Copy the header part of the wav data into our structure

    // If we have NOT found the data chunk then keep reading chunks until we find it.
    while (memcmp(wav_header->data.chunkID, "data", 4) != 0) {
        ESP_LOGI(TAG, "Skipping past WAV chunk %s", wav_header->data.chunkID);
        fread(buffer, wav_header->data.chunk_size, 1, *f); // read past these data for this chunk.
        fread(buffer, sizeof(wav_chunk_t), 1, *f); // read the start of the next chunk.
        memcpy(&wav_header->data, &buffer, sizeof(wav_chunk_t));  // Copy the data chunk header into the data header
    }

    log_wav_header(wav_header);                // Dump the header data to serial, optional!
    if (!validate_wav_data(wav_header)) {
        ESP_LOGW(TAG, "Could not validate the Start Sound - sound will not be played on start");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Maxima loaded wav header - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));

    return ESP_OK;
}

/**
 * 2 padded silences. Each one a full buffer.
 *
 * I (9233) maxima_main: Maxima loaded wav header - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=39ms free_heap=247628
 * I (9245) maxima_main: play_wav_file - Start sample_rate=16000 free_heap=247628
 * I (10394) maxima_main: play_wav_file - last chunk read nr_bytes_read=512 bytes_written=1024. Ceasing playback now
 * I (10586) maxima_main: play_wav_file - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=1333ms free_heap=247872
 *
 * 512 bytes read
 */
static void play_wav_file1(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 1024
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        const uint32_t nr_bytes = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, WAV_DATA_BUFFER_SIZE, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}


/**
 * No padded silences.
 *
 * I (72066) maxima_main: Maxima loaded wav header - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=40ms free_heap=247628
 * I (72078) maxima_main: play_wav_file - Start sample_rate=16000 free_heap=247628
 * I (73227) maxima_main: play_wav_file - last chunk read nr_bytes_read=512 bytes_written=1024. Ceasing playback now
 * I (73228) maxima_main: play_wav_file - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=1142ms free_heap=247872
 *
 * 512 bytes read
 */
static void play_wav_file2(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 1024
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        const uint32_t nr_bytes = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, WAV_DATA_BUFFER_SIZE, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

/**
 * One padded silence.
 *
 * I (26041) maxima_main: Maxima loaded wav header - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=40ms free_heap=247628
 * I (26053) maxima_main: play_wav_file - Start sample_rate=16000 free_heap=247628
 * I (27202) maxima_main: play_wav_file - last chunk read nr_bytes_read=512 bytes_written=1024. Ceasing playback now
 * I (27266) maxima_main: play_wav_file - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=1205ms free_heap=247872
 *
 * 512 bytes read !!!!
 */
static void play_wav_file3(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 1024
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        const uint32_t nr_bytes = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, WAV_DATA_BUFFER_SIZE, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

/**
 * Reading WAV_DATA_BUFFER
 * Writing all of WAV_DATA_BUFFER each time.
 */
static void play_wav_file4(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 8096
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        const uint32_t nr_bytes = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, WAV_DATA_BUFFER_SIZE, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

/**
 * No padded silences. But reading full silence buffer each time.
 * And only writing out those bytes that have been read.
 *
 * I (50234) maxima_main: Maxima loaded wav header - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=40ms free_heap=247628
 * I (50246) maxima_main: play_wav_file - Start sample_rate=16000 free_heap=247628
 * I (51331) maxima_main: play_wav_file - last chunk read nr_bytes_read=4352 bytes_written=4352. Ceasing playback now
 * I (51332) maxima_main: play_wav_file - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=1077ms free_heap=247872
 */
static void play_wav_file5(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 8096
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        const uint32_t nr_bytes = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, nr_bytes, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

/**
 * No padded silences. But reading full silence buffer each time.
 * And only writing out those bytes that have been read.
 * And then writing null bytes till the end of the next silence block.
 *
 * I (86000) maxima_main: Maxima loaded wav header - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=40ms free_heap=247628
 * I (86012) maxima_main: play_wav_file - Start sample_rate=16000 free_heap=247628
 * I (86969) maxima_main: play_wav_file - last chunk read nr_bytes_read=4352 bytes_written=4352. Ceasing playback now
 * I (87033) maxima_main: play_wav_file - Finish. filename=/OYM-USA-male-1-16000.wav Elapsed time=1013ms free_heap=247872
 */
static void play_wav_file6(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 8096
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    uint32_t nr_bytes_read = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        nr_bytes_read = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes_read == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, nr_bytes_read, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes_read != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes_read, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE - nr_bytes_read, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}


/**
 * But reading full silence buffer each time.
 * And only writing out those bytes that have been read.
 * And then writing null bytes till the end of the next silence block.
 * And then writing another full block of silence.
 */
static void play_wav_file7(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 8096
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    uint32_t nr_bytes_read = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        nr_bytes_read = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes_read == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, nr_bytes_read, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes_read != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes_read, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE - nr_bytes_read, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

/**
 * But reading full silence buffer each time.
 * And only writing out those bytes that have been read.
 * And then writing a full block of silence.
 * And then writing another full block of silence.
 */
static void play_wav_file8(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 8096
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    uint32_t nr_bytes_read = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        nr_bytes_read = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes_read == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, nr_bytes_read, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes_read != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes_read, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

/**
 * Reading WAV_DATA_BUFFER.
 * Writing all of WAV_DATA_BUFFER each time.
 * Writes full SILENCE_BUFFER.
 * Writes full SILENCE_BUFFER.
 */
static void play_wav_file9(char* filename) {

    FILE* f;
    wav_header_t wav_header;
    ESP_ERROR_CHECK(load_wav_header(filename, &wav_header, &f));

    // Set sample rate
    ESP_ERROR_CHECK(i2s_set_sample_rates(i2s_num, wav_header.SampleRate));   //set sample rate

    // Read the data and send it to I2S to play
    #define WAV_DATA_BUFFER_SIZE 8096
    ESP_LOGI(TAG, "play_wav_file - Start sample_rate=%d free_heap=%d", wav_header.SampleRate, heap_caps_get_free_size(MALLOC_CAP_8BIT));
    char* data = (char*) malloc(WAV_DATA_BUFFER_SIZE);

    const int64_t start_ms = esp_timer_get_time() / 1000;
    uint32_t nr_bytes_written;
    //ESP_ERROR_CHECK(i2s_start(i2s_num));
    while (true) {
        memset(data, 0, WAV_DATA_BUFFER_SIZE); // Clear buffer.
        const uint32_t nr_bytes = fread(data, sizeof(char), WAV_DATA_BUFFER_SIZE, f);
        if (nr_bytes == 0) {
            break;
        }
        ESP_ERROR_CHECK(i2s_write(i2s_num, data, WAV_DATA_BUFFER_SIZE, &nr_bytes_written, portMAX_DELAY));
        if (nr_bytes != WAV_DATA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "play_wav_file - last chunk read nr_bytes_read=%d bytes_written=%d. Ceasing playback now", nr_bytes, nr_bytes_written);
            break;
        }
    }
    fclose(f);
    //ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)); // Disable channel at end of playback to avoid clicking noise. Taken from https://github.com/earlephilhower/ESP8266Audio/issues/406
    //ESP_ERROR_CHECK(i2s_zero_dma_buffer(i2s_num)); // Fill dma buffer with zeroes until it is full.
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    ESP_ERROR_CHECK(i2s_write(i2s_num, SILENCE, SILENCE_SIZE, &nr_bytes_written, portMAX_DELAY)); // Write zero bytes to try to flush the remaining sound before we stop the channel
    //ESP_ERROR_CHECK(i2s_stop(i2s_num)); // Stop i2s at end of playback to avoid clicking noise
    free(data);

    ESP_LOGI(TAG, "play_wav_file - Finish. filename=%s Elapsed time=%lldms free_heap=%d", filename, (esp_timer_get_time() / 1000 - start_ms), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

void app_main(void) {
    ESP_LOGI(TAG, "Logger initialised");

    init_sound();

    // Normal start up.
    ESP_LOGI(TAG, "Finished setup");

    // loop playing WAV then pause for 3 seconds, then play again.

    while (true) {
        //play_wav_file1((char*) FILE_ON_YOUR_MARKS); // Plays OnYourMark cleanly, no buzzes, clicks or trimmed sound bytes.
        //play_wav_file2((char*) FILE_ON_YOUR_MARKS); // Plays "OnYourMark Mar" in each cycle
        //play_wav_file3((char*) FILE_ON_YOUR_MARKS); // Plays "OnYourMark (soft click)" in each cycle
        //play_wav_file4((char*) FILE_ON_YOUR_MARKS); // Plays "OnYourMark (hard click)" in each cycle
        //play_wav_file5((char*) FILE_ON_YOUR_MARKS); // Plays "OnYourMark (double-click)" in each cycle
        //play_wav_file6((char*) FILE_ON_YOUR_MARKS); // Plays "(soft click) OnYourMark (hard click)" in each cycle
        //play_wav_file7((char*) FILE_ON_YOUR_MARKS); // Plays "(soft click) OnYourMark" in each cycle

        //play_wav_file1((char*) START_TONE); // Plays Start with click at end
        //play_wav_file2((char*) START_TONE); // Plays Start with scratches at end
        //play_wav_file3((char*) START_TONE); // Plays Start with Start of next tone at end
        //play_wav_file4((char*) START_TONE); // Plays Start with scratches at end
        //play_wav_file5((char*) START_TONE); // Plays Start with scratches at end
        //play_wav_file6((char*) START_TONE); // Plays truncated Start with scratches at end
        //play_wav_file7((char*) START_TONE); // Plays truncated Start with scratches at end
        //play_wav_file8((char*) START_TONE); // Plays truncated Start with soft click at end

        // MIddle files
        // OYM_1 - CLick at end
        // Start_1 CLick at end

        //play_wav_file1((char*) FILE_ON_YOUR_MARKS_NO_MIDDLE); // click at end
        play_wav_file9((char*) FILE_ON_YOUR_MARKS_NO_MIDDLE); // click at end

        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

