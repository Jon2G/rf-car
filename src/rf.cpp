#include <libhackrf/hackrf.h>
#include <cstdio>
#include <cmath>
#include <climits>
#include <vector>
#include <unistd.h>
#include <string>
#include "rf.h"

bool RfCar::init()
{
    int result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_init() failed: (%d)\n", result);
        return false;
    }
    hackrf_device_list_t *list = hackrf_device_list();
    if (list->devicecount < 1) {
        fprintf(stderr, "No HackRF boards found.\n");
        return false;
    }
    hackrf_device_list_free(list);
    return true;
}

void RfCar::close()
{
    if (always_tx) {
        always_tx = false;
        stopTx();
    }
    hackrf_exit();
}

static int tx_callback(hackrf_transfer* transfer)
{
    RfCar *rfcar = (RfCar*) transfer->tx_ctx;
    return rfcar->txCallback(transfer->buffer, transfer->valid_length);
}

void RfCar::startTx()
{
    if (always_tx && tx_started) {
        return;
    }
    int result = hackrf_open(&device);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_open() failed: (%d)\n", result);
    }
    result = hackrf_set_sample_rate_manual(device, sample_rate, 1);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_sample_rate_set() failed: (%d)\n", result);
    }
    uint32_t baseband_filter_bw_hz = hackrf_compute_baseband_filter_bw_round_down_lt(sample_rate);
    result = hackrf_set_baseband_filter_bandwidth(device, baseband_filter_bw_hz);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_baseband_filter_bandwidth_set() failed: (%d)\n", result);
    }
    result = hackrf_set_freq(device, freq);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_set_freq() failed: (%d)\n", result);
    }
    result = hackrf_set_amp_enable(device, 1);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_set_amp_enable() failed: (%d)\n", result);
    }
    result = hackrf_set_txvga_gain(device, last_gain_tx);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_set_txvga_gain() failed: (%d)\n", result);
    }
    result = hackrf_start_tx(device, tx_callback, this);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_start_tx() failed: (%d)\n", result);
    }
    tx_started = true;
}

void RfCar::stopTx()
{
    if (always_tx) {
        return;
    }
    int result = hackrf_stop_tx(device);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_stop_tx() failed: (%d)\n", result);
    }
    tx_started = false;
    result = hackrf_close(device);
    if (result != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_close() failed: (%d)\n", result);
    }
}

void RfCar::changeState(Direction dir, int gain_tx)
{
    if (gain_tx != last_gain_tx) {
        last_gain_tx = gain_tx;
        if (tx_started) {
            int result = hackrf_set_txvga_gain(device, last_gain_tx);
            if (result != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_set_txvga_gain() failed: (%d)\n", result);
            }
        }
    }
    if (!supportDirection(dir)) {
        return;
    }
    if (dir != last_dir) {
        if (last_dir == NONE) {
            last_dir = dir;
            pos = 0;
            startTx();
            return;
        } else if (dir == NONE) {
            txEnd();
            stopTx();
        }
        last_dir = dir;
        pos = 0;
    }
}

static void push_map(std::unordered_map<int, int> &fsk_map, int ind, const std::string &data)
{
    for (auto b : data) {
        fsk_map[ind++] = b == '0' ? 0 : 1;
    }
}

FskCar::FskCar(uint64_t freq, int sample_rate, int symbol_rate, bool always_tx) : RfCar(freq, sample_rate, symbol_rate, always_tx) {
    std::string fwd_right_bits[4] = {
        "10101010101010101100101100001010010001000110110100001111000011110000111000110110100010001111110111110110110000001011100100101010000101010110100000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110000100010001111110111110110110000001011100100101010101101000100110111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100010001111110111110110110000001011100100101010110101001010111000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110100100010001111110111110110110000001011100100101010011101011000101111"
    };
    std::string back_right_bits[4] = {
        "10101010101010101100101100001010010001000110110100001111000011110000111000110000100001001111000111110110110000001011100100101010001101001000010111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100001001111000111110110110000001011100100101010010101000110011000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110100100001001111000111110110110000001011100100101010111101010100001111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110110100001001111000111110110110000001011100100101010100101011010000000"
    };
    std::string fwd_bits[4] = {
        "10101010101010101100101100001010010001000110110100001111000011110000111000110100100010101111111111110110110000001011100100101010101110100100100000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110110100010101111111111110110110000001011100100101010110110101010101111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110000100010101111111111110110110000001011100100101010011110111000111000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100010101111111111110110110000001011100100101010000110110110110111"
    };
    std::string back_bits[4] = {
        "10101010101010101100101100001010010001000110110100001111000011110000111000110110100001011111000011110110110000001011100100101010011110100101000111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110000100001011111000011110110110000001011100100101010110110110111010000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100001011111000011110110110000001011100100101010101110111001011111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110100100001011111000011110110110000001011100100101010000110101011001000"
    };
    std::string fwd_left_bits[4] = {
        "10101010101010101100101100001010010001000110110100001111000011110000111000110110100000101111011111110110110000001011100100101010110101011100010000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110000100000101111011111110110110000001011100100101010011101001110000111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100000101111011111110110110000001011100100101010000101000000001000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110100100000101111011111110110110000001011100100101010101101010010011111"
    };
    std::string back_left_bits[4] = {
        "10101010101010101100101100001010010001000110110100001111000011110000111000110110100000011111010011110110110000001011100100101010111101011111011000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110000100000011111010011110110110000001011100100101010010101001101001111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100000011111010011110110110000001011100100101010001101000011000000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110100100000011111010011110110110000001011100100101010100101010001010111"
    };
    std::string stop_bits[4] = {
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100000001111010111110110110000001011100100101010110110111100000111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110100100000001111010111110110110000001011100100101010011110101110010000",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110110100000001111010111110110110000001011100100101010000110100000011111",
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100000001111010111110110110000001011100100101010110110111100000111"
    };
    int spb = sample_rate / symbol_rate; // samples per bit
    int long_pause = (0.003641 * sample_rate) / spb; // long pause
    int short_pause = (0.000355 * sample_rate) / spb; // short pause
    int map_ind = 0;

    map_ind += long_pause;
    for (int i = 0 ; i < 9 ; i++) {
        for (int j = 0 ; j < 16 ; j++) {
            push_map(patterns[FWD_RIGHT], map_ind, fwd_right_bits[i%4]);
            push_map(patterns[BACK_RIGHT], map_ind, back_right_bits[i%4]);
            push_map(patterns[FWD], map_ind, fwd_bits[i%4]);
            push_map(patterns[BACK], map_ind, back_bits[i%4]);
            push_map(patterns[FWD_LEFT], map_ind, fwd_left_bits[i%4]);
            push_map(patterns[BACK_LEFT], map_ind, back_left_bits[i%4]);
            push_map(patterns[STOP], map_ind, stop_bits[i%4]);
            map_ind += 146;
            map_ind += short_pause;
        }
        map_ind += long_pause;
    }
    pattern_size = map_ind;

    std::string sync1_bits[4] = {
        "10101010101010101011010010110100101101001100101011001010110010101100101000110000000001111100001100011001011101010010000000101010101001011111100000",
        "10101010101010101011010010110100101101001100101011001010110010101100101000110010000001111100001100011001011101010010000000101010110001010001101111",
        "10101010101010101011010010110100101101001100101011001010110010101100101000110100000001111100001100011001011101010010000000101010011001000011111000",
        "10101010101010101011010010110100101101001100101011001010110010101100101000110110000001111100001100011001011101010010000000101010000001001101110111"
    };
    std::string sync2_bits =
        "10101010101010101100101100001010010001000110110100001111000011110000111000110010100000001111010100111011000011011011100100101010001101010100010111";
    map_ind = 0;
    map_ind += long_pause;
    for (int i = 0 ; i < 4 ; i++) {
        for (int j = 0 ; j < 16 ; j++) {
            push_map(patterns[SYNC], map_ind, sync1_bits[i]);
            map_ind += 146;
            map_ind += short_pause;
        }
        map_ind += long_pause;
    }
    map_ind += (0.01 * sample_rate) / spb;
    for (int k = 0 ; k < 5 ; k++) {
        for (int i = 0 ; i < 4 ; i++) {
            for (int j = 0 ; j < 16 ; j++) {
                push_map(patterns[SYNC], map_ind, sync2_bits);
                map_ind += 146;
                map_ind += short_pause;
            }
            map_ind += long_pause;
        }
    }
    sync_pattern_size = map_ind;
}

void FskCar::sendSync()
{
    last_dir = SYNC;
    pos = 0;
    startTx();
    int spb = sample_rate / symbol_rate; // samples per bit
    while ((int)pos/spb < sync_pattern_size) {
        // "the right way" is to use condition variable but this is more simple and works fine
        usleep(1000);
    }
    stopTx();
    last_dir = NONE;
}

void FskCar::txEnd()
{
    last_dir = STOP;
    pos = 0;
    int spb = sample_rate / symbol_rate; // samples per bit
    while ((int)pos/spb < pattern_size) {
        // "the right way" is to use condition variable but this is more simple and works fine
        usleep(1000);
    }
}

int FskCar::txCallback(uint8_t* buffer, int valid_length)
{
    int spb = sample_rate / symbol_rate; // samples per bit
    for (int i = 0 ; i < valid_length/2 ; i++) {
        int ind = pos/spb % pattern_size;
        std::unordered_map<int,int> &pattern = patterns[last_dir];
        auto it = pattern.find(ind);
        if (it != pattern.end()) {
            int bit = it->second;
            float freq = bit == 0 ? 1000000 : 1500000;
            float phase_change_per_sample = (2*M_PI * freq) / sample_rate;
            buffer[i*2] = cos(phase) * SCHAR_MAX;
            buffer[i*2+1] = sin(phase) * SCHAR_MAX;
            phase += phase_change_per_sample;
            if (phase > 2*M_PI) {
                phase -= 2*M_PI;
            }
        } else {
            buffer[i*2] = 0;
            buffer[i*2+1] = 0;
            phase = 0;
        }
        pos++;
    }
    return 0;
}

static void make_short_pulses(std::vector<int> &v, int num)
{
    for (int i = 0 ; i < num ; i++) {
        v.push_back(1);
        v.push_back(0);
    }
}

OokCar::OokCar(uint64_t freq, int sample_rate, int symbol_rate, bool always_tx) : RfCar(freq, sample_rate, symbol_rate, always_tx)
{
   for (int i = 0 ; i < 8 ; i++) {
       // each pattern start with 4 long pulses
       for (int j = 0 ; j < 4 ; j++) {
           patterns[i].push_back(1);
           patterns[i].push_back(1);
           patterns[i].push_back(1);
           patterns[i].push_back(0);
       }
   }
   make_short_pulses(patterns[FWD], 10);
   make_short_pulses(patterns[FWD_LEFT], 28);
   make_short_pulses(patterns[FWD_RIGHT], 34);
   make_short_pulses(patterns[BACK], 40);
   make_short_pulses(patterns[BACK_LEFT], 52);
   make_short_pulses(patterns[BACK_RIGHT], 46);
   make_short_pulses(patterns[LEFT], 58);
   make_short_pulses(patterns[RIGHT], 64);
   patterns[NONE].push_back(0);
   patterns[NONE].push_back(0);
   patterns[NONE].push_back(0);
   // moving averarge can be implemented more efficiently
   // but this allows playing with other type of filters
   for (int i = 0 ; i < 20 ; i++) {
       filter.push_back(0.9/20);
   }
}

void OokCar::invertSteering() {
    patterns[LEFT].swap(patterns[RIGHT]);
    patterns[FWD_LEFT].swap(patterns[FWD_RIGHT]);
    patterns[BACK_LEFT].swap(patterns[BACK_RIGHT]);
}

void OokCar::invertThrottle() {
    patterns[FWD].swap(patterns[BACK]);
    patterns[FWD_LEFT].swap(patterns[BACK_LEFT]);
    patterns[FWD_RIGHT].swap(patterns[BACK_RIGHT]);
}

int OokCar::txCallback(uint8_t *buffer, int valid_length)
{
    int spb = sample_rate / symbol_rate; // samples per bit
    for (int i = 0; i < valid_length / 2; i++)
    {
        std::vector<int> &pattern = patterns[last_dir];
        int pattern_size = pattern.size();
        float sum = 0;
        for (int j = 0; j < (int)filter.size(); j++)
        {
            int sample = pattern[((pos + j) / spb) % pattern_size];
            sum += filter[j] * sample;
        }
        pos += 1;
        buffer[i * 2] = sum * 127;
        buffer[i * 2 + 1] = 0;
    }
    return 0;
}