/*=========================================================================

  Library   : Image Registration Toolkit (IRTK)
  Module    : $Id$
  Copyright : Imperial College, Department of Computing
              Visual Information Processing (VIP), 2008 onwards
  Date      : $Date$
  Version   : $Revision$
  Changes   : $Author$

Copyright (c) IXICO LIMITED
All rights reserved.
See COPYRIGHT for details

=========================================================================*/

public:

//
// Members for the registration panel. The registration itself runs the
// registration2 library (the same code as rreg2 / areg2 / nreg2) in a
// background thread so that the viewer stays responsive.
//

Fl_Choice *registrationType;
Fl_Choice *similarityMeasure;
Fl_Check_Button *level[5];
Fl_Counter *numberOfBins;
Fl_Counter *numberOfIterations;
Fl_Counter *maxStepLength;
Fl_Counter *minStepLength;
Fl_Value_Slider *targetBlurring;
Fl_Value_Slider *targetResolution;
Fl_Value_Slider *sourceBlurring;
Fl_Value_Slider *sourceResolution;
Fl_Value_Slider *targetPadding;
Fl_Value_Slider *controlPointSpacing;
Fl_Value_Input *bendingEnergy;
Fl_Button *startRegistrationButton;
Fl_Button *guessRegistrationButton;
Fl_Button *loadParameterButton;
Fl_Button *saveParameterButton;
Fl_Output *registrationStatus;

/// Registration type menu
static Fl_Menu_Item menu_registrationType[];

/// Similarity measure menu
static Fl_Menu_Item menu_similarityMeasure[];

/// Callback for changing the registration type
static void cb_registrationType(Fl_Widget *, void *);

/// Callback for any parameter widget: copies all widget values into the parameters
static void cb_registrationParameter(Fl_Widget *, void *);

/// Callback for reading parameters from a file
static void cb_loadParameter(Fl_Button *, void *);

/// Callback for writing parameters to a file
static void cb_saveParameter(Fl_Button *, void *);

/// Callback for starting a registration
static void cb_startRegistration(Fl_Button *, void *);

/// Callback for guessing registration parameters from the images
static void cb_guessRegistrationParameter(Fl_Button *, void *);

/// Timer callback that checks whether a running registration has finished
static void cb_registrationPoll(void *);

public:

/// Update the registration control window
void UpdateRegistrationControlWindow();

/// Initialize the registration control window
void InitializeRegistrationControlWindow();
