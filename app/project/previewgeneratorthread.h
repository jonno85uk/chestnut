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

#ifndef PREVIEWGENERATORTHREAD_H
#define PREVIEWGENERATORTHREAD_H

#include <thread>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <QMetaType>

#include "footage.h"


Q_DECLARE_METATYPE(FootageWPtr)

namespace chestnut::project
{
  class PreviewGeneratorThread : public QObject
  {
      Q_OBJECT
    public:
      explicit PreviewGeneratorThread(QObject* parent=nullptr);
      ~PreviewGeneratorThread() override;

      /**
       * @brief       Add to the queue a Footage for its previews to be read/generated in turn
       * @param ftg
       * @see previewGenerated
       * @see previewFailed
       */
      void addToQueue(FootagePtr ftg);
    signals:
      /**
       * @brief       Signal to indicate that all streams in a Footage have previews
       * @param item
       */
      void previewGenerated(FootageWPtr item);
      /**
       * @brief       Signal to indicate that one or all streams in a Footage are missing previews
       * @param item
       */
      void previewFailed(FootageWPtr item);

    private:
      std::thread thread_;
      QQueue<FootageWPtr> queue_;
      QMutex mutex_;
      QMutex queue_mutex_;
      QWaitCondition wait_cond_;
      std::atomic_bool running_ {true};

      /**
       * @brief The worker method for the thread
       */
      void run();

  };
}

#endif // PREVIEWGENERATORTHREAD_H
