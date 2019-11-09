#include "viewertest.h"

#include <QtTest>

#include "panels/viewer.h"

ViewerTest::ViewerTest(QObject *parent) : QObject(parent)
{

}



void ViewerTest::testCaseCreateFootageSequenceAudioVideoClip()
{
  // create a seemingly valid media instance. 1 audio track. 1 video track
  auto mda = std::make_shared<Media>();
  auto ftg = std::make_shared<Footage>();
  mda->setFootage(ftg);

  auto strm = std::make_shared<project::FootageStream>(ftg, nullptr);
  ftg->videoTracks().append(strm);
  ftg->audioTracks().append(strm);

  Viewer vwr;
  auto sqn = vwr.createFootageSequence(mda);
  QVERIFY(sqn->clips().size() == 2);
}
