/* 
 * Olive. Olive is a free non-linear video editor for Windows, macOS, and Linux.
 * Copyright (C) 2018  {{ organization }}
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
 *along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "mediapropertiesdialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QTreeWidgetItem>
#include <QGroupBox>
#include <QListWidget>
#include <QSpinBox>

#include "project/footage.h"
#include "project/media.h"
#include "panels/project.h"
#include "project/undo.h"

MediaPropertiesDialog::MediaPropertiesDialog(QWidget *parent, MediaPtr i) :
	QDialog(parent),
	item(i)
{
    setWindowTitle(tr("\"%1\" Properties").arg(i->get_name()));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	QGridLayout* grid = new QGridLayout();
	setLayout(grid);

	int row = 0;

    FootagePtr  f = item->get_object<Footage>();

    grid->addWidget(new QLabel(tr("Tracks:")), row, 0, 1, 2);
	row++;

	track_list = new QListWidget();
	for (int i=0;i<f->video_tracks.size();i++) {
		const FootageStream& fs = f->video_tracks.at(i);

        QListWidgetItem* item = new QListWidgetItem(
                    tr("Video %1: %2x%3 %4FPS").arg(
                        QString::number(fs.file_index),
                        QString::number(fs.video_width),
                        QString::number(fs.video_height),
                        QString::number(fs.video_frame_rate)
                    )
                );
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(fs.enabled ? Qt::Checked : Qt::Unchecked);
		item->setData(Qt::UserRole+1, fs.file_index);
		track_list->addItem(item);
	}
	for (int i=0;i<f->audio_tracks.size();i++) {
		const FootageStream& fs = f->audio_tracks.at(i);
        QListWidgetItem* item = new QListWidgetItem(
                    tr("Audio %1: %2Hz %3 channels").arg(
                        QString::number(fs.file_index),
                        QString::number(fs.audio_frequency),
                        QString::number(fs.audio_channels)
                    )
                );
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(fs.enabled ? Qt::Checked : Qt::Unchecked);
		item->setData(Qt::UserRole+1, fs.file_index);
		track_list->addItem(item);
	}
	grid->addWidget(track_list, row, 0, 1, 2);
	row++;

	if (f->video_tracks.size() > 0) {
		// frame conforming
		if (!f->video_tracks.at(0).infinite_length) {
            grid->addWidget(new QLabel(tr("Conform to Frame Rate:")), row, 0);
			conform_fr = new QDoubleSpinBox();
			conform_fr->setMinimum(0.01);
			conform_fr->setValue(f->video_tracks.at(0).video_frame_rate * f->speed);
			grid->addWidget(conform_fr, row, 1);
		}

		row++;

		// deinterlacing mode
		interlacing_box = new QComboBox();        
        interlacing_box->addItem(
                    tr("Auto (%1)").arg(
                        get_interlacing_name(f->video_tracks.at(0).video_auto_interlacing)
                    )
                );
		interlacing_box->addItem(get_interlacing_name(VIDEO_PROGRESSIVE));
		interlacing_box->addItem(get_interlacing_name(VIDEO_TOP_FIELD_FIRST));
		interlacing_box->addItem(get_interlacing_name(VIDEO_BOTTOM_FIELD_FIRST));

        interlacing_box->setCurrentIndex(
                    (f->video_tracks.at(0).video_auto_interlacing == f->video_tracks.at(0).video_interlacing)
                    ? 0
                    : f->video_tracks.at(0).video_interlacing + 1);

        grid->addWidget(new QLabel(tr("Interlacing:")), row, 0);
		grid->addWidget(interlacing_box, row, 1);

		row++;
	}

	name_box = new QLineEdit(item->get_name());
    grid->addWidget(new QLabel(tr("Name:")), row, 0);
	grid->addWidget(name_box, row, 1);
	row++;

	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->setCenterButtons(true);
	grid->addWidget(buttons, row, 0, 1, 2);

	connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
}

void MediaPropertiesDialog::accept() {
    FootagePtr f = item->get_object<Footage>();

	ComboAction* ca = new ComboAction();

	// set track enable
	for (int i=0;i<track_list->count();i++) {
		QListWidgetItem* item = track_list->item(i);
		const QVariant& data = item->data(Qt::UserRole+1);
		if (!data.isNull()) {
			int index = data.toInt();
			bool found = false;
			for (int j=0;j<f->video_tracks.size();j++) {
				if (f->video_tracks.at(j).file_index == index) {
					f->video_tracks[j].enabled = (item->checkState() == Qt::Checked);
					found = true;
					break;
				}
			}
			if (!found) {
				for (int j=0;j<f->audio_tracks.size();j++) {
					if (f->audio_tracks.at(j).file_index == index) {
						f->audio_tracks[j].enabled = (item->checkState() == Qt::Checked);
						break;
					}
				}
			}
		}
	}

	bool refresh_clips = false;

	// set interlacing
	if (f->video_tracks.size() > 0) {
		if (interlacing_box->currentIndex() > 0) {
			ca->append(new SetInt(&f->video_tracks[0].video_interlacing, interlacing_box->currentIndex() - 1));
		} else {
			ca->append(new SetInt(&f->video_tracks[0].video_interlacing, f->video_tracks.at(0).video_auto_interlacing));
		}

		// set frame rate conform
		if (!f->video_tracks.at(0).infinite_length) {
			if (!qFuzzyCompare(conform_fr->value(), f->video_tracks.at(0).video_frame_rate)) {
				ca->append(new SetDouble(&f->speed, f->speed, conform_fr->value()/f->video_tracks.at(0).video_frame_rate));
				refresh_clips = true;
			}
		}
	}

	// set name
	MediaRename* mr = new MediaRename(item, name_box->text());

	ca->append(mr);
	ca->appendPost(new CloseAllClipsCommand());
	ca->appendPost(new UpdateFootageTooltip(item));
	if (refresh_clips) ca->appendPost(new RefreshClips(item));

	e_undo_stack.push(ca);

	QDialog::accept();
}
