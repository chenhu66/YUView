﻿/*  This file is part of YUView - The YUV player with advanced analytics toolset
 *   <https://github.com/IENT/YUView>
 *   Copyright (C) 2015  Institut für Nachrichtentechnik, RWTH Aachen University, GERMANY
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   In addition, as a special exception, the copyright holders give
 *   permission to link the code of portions of this program with the
 *   OpenSSL library under certain conditions as described in each
 *   individual source file, and distribute linked combinations including
 *   the two.
 *
 *   You must obey the GNU General Public License in all respects for all
 *   of the code used other than OpenSSL. If you modify file(s) with this
 *   exception, you may extend this exception to your version of the
 *   file(s), but you are not obligated to do so. If you do not wish to do
 *   so, delete this exception statement from your version. If you delete
 *   this exception statement from all source files in the program, then
 *   also delete it here.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AnnexBVVC.h"

#include <algorithm>
#include <cmath>

#include "nal_unit_header.h"
#include "parser/common/Macros.h"
#include "parser/common/ReaderHelper.h"
#include "seq_parameter_set_rbsp.h"
#include "video_parameter_set_rbsp.h"

#define PARSER_VVC_DEBUG_OUTPUT 0
#if PARSER_VVC_DEBUG_OUTPUT && !NDEBUG
#include <QDebug>
#define DEBUG_VVC(msg) qDebug() << msg
#else
#define DEBUG_VVC(msg) ((void)0)
#endif

namespace parser
{

using namespace vvc;

double AnnexBVVC::getFramerate() const { return DEFAULT_FRAMERATE; }

QSize AnnexBVVC::getSequenceSizeSamples() const { return QSize(352, 288); }

yuvPixelFormat AnnexBVVC::getPixelFormat() const { return yuvPixelFormat(Subsampling::YUV_420, 8); }

QList<QByteArray> AnnexBVVC::getSeekFrameParamerSets(int iFrameNr, uint64_t &filePos)
{
  Q_UNUSED(iFrameNr);
  Q_UNUSED(filePos);
  return {};
}

QByteArray AnnexBVVC::getExtradata() { return {}; }

QPair<int, int> AnnexBVVC::getProfileLevel() { return QPair<int, int>(0, 0); }

Ratio AnnexBVVC::getSampleAspectRatio() { return Ratio({1, 1}); }

AnnexB::ParseResult
AnnexBVVC::parseAndAddNALUnit(int                                           nalID,
                              QByteArray                                    data_,
                              std::optional<BitratePlotModel::BitrateEntry> bitrateEntry,
                              std::optional<pairUint64>                     nalStartEndPosFile,
                              TreeItem *                                    parent)
{
  AnnexB::ParseResult parseResult;

  // Convert QByteArray to ByteVector. This is just a temporary solution.
  // Once all parsing functions are switched we can change the interface and get rid of this.
  auto data = ReaderHelperNew::convertBeginningToByteArray(data_);

  if (nalID == -1 && data_.isEmpty())
    return parseResult;

  // Skip the NAL unit header
  int readOffset = 0;
  if (data.at(0) == (char)0 && data.at(1) == (char)0 && data.at(2) == (char)1)
    readOffset = 3;
  else if (data.at(0) == (char)0 && data.at(1) == (char)0 && data.at(2) == (char)0 &&
           data.at(3) == (char)1)
    readOffset = 4;

  // Use the given tree item. If it is not set, use the nalUnitMode (if active).
  // Create a new TreeItem root for the NAL unit. We don't set data (a name) for this item
  // yet. We want to parse the item and then set a good description.
  TreeItem *nalRoot = nullptr;
  if (parent)
    nalRoot = new TreeItem(parent);
  else if (!packetModel->isNull())
    nalRoot = new TreeItem(packetModel->getRootItem());

  parseResult.success = true;

  AnnexB::logNALSize(data, nalRoot, nalStartEndPosFile);

  ReaderHelperNew reader(data, nalRoot, "", readOffset);

  std::string specificDescription;
  auto nalVVC = std::make_shared<vvc::NalUnitVVC>(nalID, nalStartEndPosFile);
  try
  {
    nalVVC->header.parse(reader);

    if (nalVVC->header.nal_unit_type == NalType::VPS_NUT)
    {
      specificDescription = " VPS";
      auto newVPS         = std::make_unique<video_parameter_set_rbsp>();
      newVPS->parse(reader);

      this->activeParameterSets.vpsMap[newVPS->vps_video_parameter_set_id] = nalVVC;

      specificDescription += " ID " + std::to_string(newVPS->vps_video_parameter_set_id);

      nalVVC->rbsp = std::move(newVPS);
    }
    else if (nalVVC->header.nal_unit_type == NalType::SPS_NUT)
    {
      specificDescription = " SPS";
      auto newSPS         = std::make_unique<seq_parameter_set_rbsp>();
      newSPS->parse(reader);

      this->activeParameterSets.spsMap[newSPS->sps_seq_parameter_set_id] = nalVVC;

      specificDescription += " ID " + std::to_string(newSPS->sps_seq_parameter_set_id);

      nalVVC->rbsp = std::move(newSPS);
    }
  }
  catch (const std::exception &e)
  {
    specificDescription += " ERROR " + std::string(e.what());
    parseResult.success = false;
  }

  // if (nal_vvc.isAUDelimiter())
  // {
  //   DEBUG_VVC("Start of new AU. Adding bitrate " << sizeCurrentAU);

  //   BitratePlotModel::BitrateEntry entry;
  //   if (bitrateEntry)
  //   {
  //     entry.pts      = bitrateEntry->pts;
  //     entry.dts      = bitrateEntry->dts;
  //     entry.duration = bitrateEntry->duration;
  //   }
  //   else
  //   {
  //     entry.pts      = counterAU;
  //     entry.dts      = counterAU; // TODO: Not true. We need to parse the VVC header data
  //     entry.duration = 1;
  //   }
  //   entry.bitrate            = sizeCurrentAU;
  //   entry.keyframe           = false; // TODO: Also not correct. We need parsing.
  //   parseResult.bitrateEntry = entry;

  //   if (counterAU > 0)
  //   {
  //     const bool curFrameIsRandomAccess = (counterAU == 1);
  //     if (!addFrameToList(counterAU, curFrameFileStartEndPos, curFrameIsRandomAccess))
  //     {
  //       ReaderHelper::addErrorMessageChildItem(QString("Error adding frame to frame list."),
  //                                              parent);
  //       return parseResult;
  //     }
  //     if (curFrameFileStartEndPos)
  //       DEBUG_VVC("Adding start/end " << curFrameFileStartEndPos->first << "/"
  //                                     << curFrameFileStartEndPos->second << " - POC " <<
  //                                     counterAU
  //                                     << (curFrameIsRandomAccess ? " - ra" : ""));
  //     else
  //       DEBUG_VVC("Adding start/end %d/%d - POC NA/NA" << (curFrameIsRandomAccess ? " - ra" :
  //       ""));
  //   }
  //   curFrameFileStartEndPos = nalStartEndPosFile;
  //   sizeCurrentAU           = 0;
  //   counterAU++;
  // }
  // else if (curFrameFileStartEndPos && nalStartEndPosFile)
  //   curFrameFileStartEndPos->second = nalStartEndPosFile->second;

  sizeCurrentAU += data.size();

  if (nalRoot)
  {
    auto name = "NAL " + std::to_string(nalVVC->nalIdx) + ": " +
                std::to_string(nalVVC->header.nalUnitTypeID) + specificDescription;
    nalRoot->setProperties(name);
  }

  return parseResult;
}

} // namespace parser