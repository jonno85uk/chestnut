/*
 * Chestnut. Chestnut is a free non-linear video editor for Linux.
 * Copyright (C) 2019
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef AUDIOWAVEFORMGENERATOR_H
#define AUDIOWAVEFORMGENERATOR_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

namespace chestnut::io
{
  class IWaveformGeneratedEvent
  {
    public:
      virtual ~IWaveformGeneratedEvent() {}
      /**
       * @brief           Event handler for generated waveform
       * @param data_path Where the waveform data is stored
       */
      virtual void onWaveformGenerated(std::string data_path) = 0;
  };

  using WGEPtr = std::shared_ptr<IWaveformGeneratedEvent>;

  class AudioWaveformGenerator
  {
    public:
      AudioWaveformGenerator();
      ~AudioWaveformGenerator();

      /**
       * @brief             Add a source file to the waveform generator queue
       * @param file_path   Source file
       * @param destination Where the waveform data is to be stored
       * @param stream      Which stream in the source file to use
       * @param caller      The object to call on waveform generation
       */
      void addToQueue(std::string file_path, std::string destination, const int stream, WGEPtr caller);

    private:
      std::thread thread_;
      std::mutex mutex_;
      std::condition_variable cv_;
      std::atomic_bool running_ {true};

      std::vector<std::tuple<std::string, std::string, int, WGEPtr>> queue_;

      void run();

      void generate(std::string source_path, std::string storage_path, const int stream, WGEPtr caller) const;

  };
}

#endif // AUDIOWAVEFORMGENERATOR_H
