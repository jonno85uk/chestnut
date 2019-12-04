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


#include "audiowaveformgenerator.h"

#include <fmt/core.h>
#include <stdlib.h>

#include "debug.h"


using chestnut::io::AudioWaveformGenerator;

constexpr auto EXTENSION = "dat";
#ifdef __linux__
constexpr auto CMD_FORMAT = "ffmpeg -i \"{0}\" -map 0:{1} -f wav - | audiowaveform --input-format wav --output-format {2} -b 8 "
                            "--split-channels -o \"{3}.{2}\"";
#elif _WIN64
//TODO:
constexpr auto CMD_FORMAT = "";
#endif


AudioWaveformGenerator::AudioWaveformGenerator()
{
  thread_ = std::thread(&AudioWaveformGenerator::run, this);
}


AudioWaveformGenerator::~AudioWaveformGenerator()
{
  running_ = false;
  cv_.notify_one();
  thread_.join();
}

void AudioWaveformGenerator::addToQueue(std::string file_path,
                                        std::string destination,
                                        const int stream,
                                        chestnut::io::WGEPtr caller)
{
  qDebug() << "Adding";
  queue_.push_back(std::make_tuple(std::move(file_path), std::move(destination), stream, std::move(caller)));
  cv_.notify_one();
}


void AudioWaveformGenerator::run()
{
  while (running_) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock);
    if (!queue_.empty()) {
      auto [path, destination, stream, caller] = queue_.front();
      generate(path, destination, stream, caller);
    }
  }
  qInfo() << "Exiting";
}


void AudioWaveformGenerator::generate(std::string source_path,
                                      std::string storage_path,
                                      const int stream,
                                      WGEPtr caller) const
{

  const auto cmd = fmt::format(CMD_FORMAT, source_path, stream, EXTENSION, storage_path);
  qDebug() << cmd.c_str();
  system(cmd.c_str());
  caller->onWaveformGenerated(storage_path + "." + EXTENSION);
}
