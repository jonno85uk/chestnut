#include "timelineinfo.h"

using project::TimelineInfo;

TimelineInfo::TimelineInfo()
{

}


bool TimelineInfo::isVideo() const
{
  return track_ < 0;
}