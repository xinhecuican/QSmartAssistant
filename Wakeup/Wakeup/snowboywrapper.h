#pragma once

#include <cstdint>

class Snowboy {
 public:
  struct Model {
    const char* filename;
    float sensitivity;
  };

  Snowboy(const char* resource_name, Model model);

  int SampleRate() const;
  int NumChannels() const;
  int BitsPerSample() const;

  int RunDetection(const int16_t* data, int num_samples);

 private:
  void* detector_;
};