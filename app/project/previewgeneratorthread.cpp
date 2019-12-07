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

#include "previewgeneratorthread.h"

#include "debug.h"

using chestnut::project::PreviewGeneratorThread;

PreviewGeneratorThread::PreviewGeneratorThread(QObject* parent) : QObject(parent)
{
   // No idea how to do similar with QThread unless subclassing it, which is the "wrong" way
  thread_ = std::thread(&PreviewGeneratorThread::run, this);
}

PreviewGeneratorThread::~PreviewGeneratorThread()
{
  running_ = false;
  wait_cond_.wakeAll();
  thread_.join();
}

void PreviewGeneratorThread::addToQueue(FootagePtr ftg)
{
  if (ftg == nullptr) {
    return;
  }
  QMutexLocker lock(&queue_mutex_);
  queue_.enqueue(ftg);
  lock.unlock();
  qDebug() << "Added footage to preview generator queue, file_path:" << ftg->location();
  wait_cond_.wakeAll();
}


void PreviewGeneratorThread::run()
{
  qDebug() << "Starting Preview Generator thread";
  while (running_) {
    wait_cond_.wait(&mutex_);
    QMutexLocker lock(&queue_mutex_);
    while (!queue_.empty()) {
      if (auto ftg = queue_.dequeue().lock()) {
        lock.unlock();
        if (ftg->generatePreviews()) {
          emit previewGenerated(ftg);
          qInfo() << "Footage generated preview(s), file_path:" << ftg->location();
        } else {
          emit previewFailed(ftg);
          qWarning() << "Footage failed to generate preview(s), file_path:" << ftg->location();
        }
      } else {
        lock.unlock();
        qWarning() << "Queued Footage is null";
      }
    }
  }
  mutex_.unlock();
  qWarning() << "Exiting Preview Generator thread";
}
