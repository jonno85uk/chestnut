#include "footagetest.h"
#include "project/footage.h"
#include "project/media.h"

FootageTest::FootageTest(QObject *parent) : QObject(parent)
{

}


void FootageTest::testCaseIsImage()
{
  Footage ftg(nullptr);
  ftg.has_preview_ = true;
  // by default, not an image
  QVERIFY(ftg.isImage() == false);
  // the sole stream is a video-type and is infinitely long
  auto stream = std::make_shared<project::FootageStream>();
  stream->infinite_length = true;
  ftg.video_tracks.append(stream);
  QVERIFY(ftg.isImage() == true);
  // video-stream has a defined length -> not an image
  stream->infinite_length = false;
  QVERIFY(ftg.isImage() == false);
  // audio and video streams -> not an image
  stream->infinite_length = true;
  ftg.audio_tracks.append(stream);
  QVERIFY(ftg.isImage() == false);
}


void FootageTest::testCaseUninitSave()
{
  Footage ftg(nullptr);
  QString output;
  QXmlStreamWriter stream(&output);
  QVERIFY_EXCEPTION_THROWN(ftg.save(stream), std::runtime_error);
}

void FootageTest::testCaseNullMediaSave()
{
  Footage ftg(nullptr);
  ftg.has_preview_ = true;
  QString output;
  QXmlStreamWriter stream(&output);
  QVERIFY_EXCEPTION_THROWN(ftg.save(stream), std::runtime_error);
}

void FootageTest::testCaseSave()
{
  // The top level in project-structure
  auto root_mda = std::make_shared<Media>();

  auto parent_mda = std::make_shared<Media>(root_mda);
  Footage ftg(parent_mda);
  ftg.has_preview_ = false;

  QString output;
  QXmlStreamWriter stream(&output);

  QVERIFY(ftg.save(stream) == true);
  QVERIFY(output.startsWith("<footage") == true);
  QVERIFY(output.endsWith("</footage>") == true);
}

void  FootageTest::testCaseSaveAndLoad()
{
  constexpr auto speed = 123.456;
  constexpr auto url = "/foo/bar/spam.eggs";
  constexpr auto length = 1'000'000;
  constexpr auto folder = 1;
  constexpr auto in_point = 123;
  constexpr auto out_point = 456;
  constexpr auto using_inout = true;

  auto root_mda = std::make_shared<Media>();

  auto parent_mda = std::make_shared<Media>(root_mda);
  Footage ftg(parent_mda);
  ftg.speed_ = speed;
  ftg.url_ = url;
  ftg.length_ = length;
  ftg.folder_ = folder;
  ftg.in = in_point;
  ftg.out = out_point;
  ftg.using_inout = using_inout;

  QString output;
  QXmlStreamWriter stream(&output);
  QVERIFY(ftg.save(stream) == true);
  // Try to load what was just saved
  QXmlStreamReader input(output);
  QVERIFY(ftg.load(input) == true);
  QCOMPARE(ftg.speed_, speed);
  QCOMPARE(ftg.url_, url);
  QCOMPARE(ftg.length_, length);
  QCOMPARE(ftg.folder_, folder);
  QCOMPARE(ftg.in, in_point);
  QCOMPARE(ftg.out, out_point);
  QCOMPARE(ftg.using_inout, using_inout);
}
