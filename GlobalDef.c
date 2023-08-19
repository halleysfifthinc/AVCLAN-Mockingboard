/*
  Copyright (C) 2006 Marcin Slonicki <marcin@softservice.com.pl>.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

 -----------------------------------------------------------------------
        this file is a part of the TOYOTA Corolla MP3 Player Project
 -----------------------------------------------------------------------
                http://www.softservice.com.pl/corolla/avc

 May 28 / 2009	- version 2

*/

#include "GlobalDef.h"

// max 10 events in fifo
byte EventCount;
byte EventCmd[10];
byte Event;

byte showLog;
byte showLog2;
