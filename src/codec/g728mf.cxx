/*
 * g728mf.cxx
 *
 * G.728 Media Format descriptions
 *
 * Open Phone Abstraction Library
 * Formally known as the Open H323 project.
 *
 * Copyright (c) 2008 Vox Lucida
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Open Phone Abstraction Library
 *
 * The Initial Developer of the Original Code is Vox Lucida
 *
 * Contributor(s): ______________________________________.
 *
 * $Revision$
 * $Author$
 * $Date$
 */

#include <ptlib.h>

#include <opal/mediafmt.h>


#define new PNEW


/////////////////////////////////////////////////////////////////////////////

#if OPAL_AUDIO

const OpalAudioFormat & GetOpalG728()
{
  static const OpalAudioFormat G728(OPAL_G728, RTP_DataFrame::G728,  "G728", 5, 20, 100, 10, 256, 8000);
  return G728;
}

#endif


// End of File ///////////////////////////////////////////////////////////////
