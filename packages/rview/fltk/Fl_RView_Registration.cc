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

// Registration panel for rview.
//
// The original panel was written against an older registration API that no
// longer exists. This version drives the registration2 library, i.e. the same
// classes that rreg2, areg2 and nreg2 use. Parameters start from the library's
// own GuessParameter() defaults, so pressing Start without touching anything
// gives the same result as running the command-line tool without -parin.

#include <irtkImage.h>
#include <irtkTransformation.h>
#include <irtkRegistration2.h>
#include <irtkImageRigidRegistration2.h>
#include <irtkImageAffineRegistration2.h>
#include <irtkImageFreeFormRegistration2.h>

#include <atomic>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

// Local includes
#include <Fl_RViewUI.h>

#ifdef HAS_REGISTRATION_PANEL

extern Fl_RViewUI  *rviewUI;
extern Fl_RView    *viewer;
extern irtkRView    *rview;

namespace {

enum RegistrationType { Reg_Rigid = 0, Reg_Affine = 1, Reg_NonRigid = 2 };

const char *registrationTypeName[] = { "rigid", "affine", "nonrigid" };

/// Registration parameters shown in the panel. Multi-resolution values are
/// given for level 1; each further level doubles blurring and resolution, the
/// same convention the parameter files use for values given without a level.
struct RegistrationParameters
{
  int    type;
  int    levels;
  int    bins;
  int    iterations;
  double maxStep;
  double minStep;
  double targetBlurring;
  double targetResolution;
  double sourceBlurring;
  double sourceResolution;
  double targetPadding;
  double spacing;   // non-rigid only: control point spacing (mm)
  double lambda1;   // non-rigid only: bending energy weight
  irtkSimilarityMeasure similarity;
  bool   guessed;
};

RegistrationParameters params = { Reg_Rigid, 3, 64, 40, 1.0, 0.01, 1, 1, 1, 1, MIN_GREY, 20, 0, NMI, false };

/// Gives the panel access to the protected parameters of a registration2 class.
class RegistrationAccess
{
public:
  virtual ~RegistrationAccess() {}
  virtual irtkImageRegistration2 *Registration() = 0;
  virtual void Get(RegistrationParameters &) = 0;
  virtual void Put(const RegistrationParameters &) = 0;
};

template <class Base>
class RViewRegistration : public Base, public RegistrationAccess
{
public:
  irtkImageRegistration2 *Registration() { return this; }

  void Get(RegistrationParameters &p)
  {
    p.levels           = this->_NumberOfLevels;
    p.bins             = this->_NumberOfBins;
    p.similarity       = this->_SimilarityMeasure;
    p.iterations       = this->_NumberOfIterations[0];
    p.maxStep          = this->_MaxStep[0];
    p.minStep          = this->_MinStep[0];
    p.targetBlurring   = this->_TargetBlurring[0];
    p.targetResolution = this->_TargetResolution[0][0];
    p.sourceBlurring   = this->_SourceBlurring[0];
    p.sourceResolution = this->_SourceResolution[0][0];
    p.targetPadding    = this->_TargetPadding;
    GetExtra(p);
  }

  void Put(const RegistrationParameters &p)
  {
    int i;

    this->_NumberOfLevels    = p.levels;
    this->_NumberOfBins      = p.bins;
    this->_SimilarityMeasure = p.similarity;
    this->_TargetPadding     = p.targetPadding;
    for (i = 0; i < MAX_NO_RESOLUTIONS; i++) {
      this->_NumberOfIterations[i] = p.iterations;
      this->_MaxStep[i]            = p.maxStep;
      this->_MinStep[i]            = p.minStep;
    }
    PutResolution(this->_TargetBlurring, this->_TargetResolution, p.targetBlurring, p.targetResolution, this->_target->GetZ() > 1);
    PutResolution(this->_SourceBlurring, this->_SourceResolution, p.sourceBlurring, p.sourceResolution, this->_source->GetZ() > 1);
    PutExtra(p);
  }

  void GetExtra(RegistrationParameters &) {}
  void PutExtra(const RegistrationParameters &) {}

private:
  /// Level 1 gets the panel values, each further level doubles them. The panel
  /// sets the x resolution; y and z keep the ratio GuessParameter chose, so
  /// anisotropic voxels and 2D images are handled the same way as on the
  /// command line.
  static void PutResolution(double *blurring, double (*resolution)[3], double b0, double r0, bool is3D)
  {
    int i;
    double yratio = (resolution[0][0] > 0) ? resolution[0][1] / resolution[0][0] : 1;
    double zratio = (resolution[0][0] > 0) ? resolution[0][2] / resolution[0][0] : 1;
    double z0 = resolution[0][2];

    for (i = 0; i < MAX_NO_RESOLUTIONS; i++) {
      double f = pow(2.0, double(i));
      blurring[i]      = b0 * f;
      resolution[i][0] = r0 * f;
      resolution[i][1] = r0 * yratio * f;
      resolution[i][2] = is3D ? r0 * zratio * f : z0;
    }
  }
};

template <>
void RViewRegistration<irtkImageFreeFormRegistration2>::GetExtra(RegistrationParameters &p)
{
  p.spacing = this->_DX;
  p.lambda1 = this->_Lambda1;
}

template <>
void RViewRegistration<irtkImageFreeFormRegistration2>::PutExtra(const RegistrationParameters &p)
{
  this->_DX      = p.spacing;
  this->_DY      = p.spacing;
  this->_DZ      = p.spacing;
  this->_Lambda1 = p.lambda1;
}

RegistrationAccess *NewRegistration(int type)
{
  switch (type) {
  case Reg_Rigid:
    return new RViewRegistration<irtkImageRigidRegistration2>;
  case Reg_Affine:
    return new RViewRegistration<irtkImageAffineRegistration2>;
  default:
    return new RViewRegistration<irtkImageFreeFormRegistration2>;
  }
}

/// Copy any rview image into a real-valued image, which registration2 expects.
void ToRealImage(irtkImage *in, irtkRealImage &out)
{
  int x, y, z, t;

  out.Initialize(in->GetImageAttributes());
  for (t = 0; t < in->GetT(); t++) {
    for (z = 0; z < in->GetZ(); z++) {
      for (y = 0; y < in->GetY(); y++) {
        for (x = 0; x < in->GetX(); x++) {
          out.Put(x, y, z, t, in->GetAsDouble(x, y, z, t));
        }
      }
    }
  }
}

/// Target and source images as used by the registration. The target is cut to
/// the region of interest selected in the viewer.
void GetRegistrationImages(irtkRealImage &target, irtkRealImage &source)
{
  double x1, y1, z1, x2, y2, z2;
  int i1, j1, k1, i2, j2, k2;

  ToRealImage(rview->GetTarget(), target);
  ToRealImage(rview->GetSource(), source);

  rview->GetROI(x1, y1, z1, x2, y2, z2);
  target.WorldToImage(x1, y1, z1);
  target.WorldToImage(x2, y2, z2);

  i1 = max(0, int(round(min(x1, x2))));
  j1 = max(0, int(round(min(y1, y2))));
  k1 = max(0, int(round(min(z1, z2))));
  i2 = min(target.GetX(), int(round(max(x1, x2))) + 1);
  j2 = min(target.GetY(), int(round(max(y1, y2))) + 1);
  k2 = min(target.GetZ(), int(round(max(z1, z2))) + 1);

  if ((i2 > i1) && (j2 > j1) && (k2 > k1) &&
      ((i1 > 0) || (j1 > 0) || (k1 > 0) ||
       (i2 < target.GetX()) || (j2 < target.GetY()) || (k2 < target.GetZ()))) {
    target = target.GetRegion(i1, j1, k1, i2, j2, k2);
  }
}

/// Starting estimate for the registration, taken from the transformation that
/// is currently loaded in rview. That way rigid -> affine -> non-rigid can be
/// run one after the other, each starting where the previous one ended.
irtkTransformation *InitialTransformation(int type)
{
  int i;
  irtkTransformation *current = rview->GetTransformation();
  irtkAffineTransformation *affine = dynamic_cast<irtkAffineTransformation *>(current);
  irtkMultiLevelFreeFormTransformation *mffd = dynamic_cast<irtkMultiLevelFreeFormTransformation *>(current);

  switch (type) {
  case Reg_Rigid: {
    irtkRigidTransformation *rigid = new irtkRigidTransformation;
    if (affine != NULL) {
      for (i = 0; i < rigid->NumberOfDOFs(); i++) rigid->Put(i, affine->irtkAffineTransformation::Get(i));
    }
    return rigid;
  }
  case Reg_Affine: {
    irtkAffineTransformation *result = new irtkAffineTransformation;
    if (affine != NULL) {
      for (i = 0; i < result->NumberOfDOFs(); i++) result->Put(i, affine->irtkAffineTransformation::Get(i));
    }
    return result;
  }
  default: {
    if (mffd != NULL) return new irtkMultiLevelFreeFormTransformation(*mffd);
    irtkAffineTransformation global;
    if (affine != NULL) {
      for (i = 0; i < global.NumberOfDOFs(); i++) global.Put(i, affine->irtkAffineTransformation::Get(i));
    }
    return new irtkMultiLevelFreeFormTransformation(global);
  }
  }
}

bool ImagesLoaded()
{
  return (rview->GetTarget()->IsEmpty() != true) && (rview->GetSource()->IsEmpty() != true);
}

/// Creates a registration of the current type with the current images,
/// guesses its parameters, and returns it ready to use. The images and the
/// output transformation are owned by the caller.
RegistrationAccess *SetUpRegistration(irtkRealImage &target, irtkRealImage &source, irtkTransformation *output)
{
  RegistrationAccess *access = NewRegistration(params.type);
  access->Registration()->SetInput(&target, &source);
  access->Registration()->SetOutput(output);
  access->Registration()->GuessParameter();
  return access;
}

/// Replace the panel parameters by the library's guess for the current images.
void GuessParameters()
{
  irtkRealImage target, source;

  GetRegistrationImages(target, source);
  irtkTransformation *output = InitialTransformation(params.type);
  RegistrationAccess *access = SetUpRegistration(target, source, output);
  access->Get(params);
  params.guessed = true;
  delete access;
  delete output;
}

/// A registration running in the background.
struct RegistrationJob
{
  irtkRealImage target;
  irtkRealImage source;
  irtkTransformation *transformation;
  RegistrationAccess *access;
  int type;
  std::atomic<bool> done;
  std::thread thread;
};

RegistrationJob *job = NULL;
int registrationCount = 0;
char outputDir[1024] = "";

/// Directory for registration results. Each result is written there so that
/// it appears in the transformation list like any loaded .dof file.
const char *OutputDirectory()
{
  if (outputDir[0] == '\0') {
    const char *tmp = getenv("TMPDIR");
    snprintf(outputDir, sizeof(outputDir), "%s/rview-registration-XXXXXX", (tmp != NULL) ? tmp : "/tmp");
    if (mkdtemp(outputDir) == NULL) {
      snprintf(outputDir, sizeof(outputDir), ".");
    }
  }
  return outputDir;
}

void SetRunning(bool running)
{
  if (running) {
    rviewUI->startRegistrationButton->deactivate();
    rviewUI->guessRegistrationButton->deactivate();
    rviewUI->loadParameterButton->deactivate();
    rviewUI->registrationType->deactivate();
  } else {
    rviewUI->startRegistrationButton->activate();
    rviewUI->guessRegistrationButton->activate();
    rviewUI->loadParameterButton->activate();
    rviewUI->registrationType->activate();
  }
}

} // namespace

Fl_Menu_Item Fl_RViewUI::menu_registrationType[] = {
  {"Rigid",     0, 0, 0},
  {"Affine",    0, 0, 0},
  {"Non-rigid", 0, 0, 0},
  {0}
};

// registration2 only implements SSD and NMI
Fl_Menu_Item Fl_RViewUI::menu_similarityMeasure[] = {
  {"NMI", 0, 0, 0},
  {"SSD", 0, 0, 0},
  {0}
};

void Fl_RViewUI::cb_registrationType(Fl_Widget *, void *)
{
  params.type = rviewUI->registrationType->value();

  // Each type has its own defaults (e.g. levels and step length)
  if (ImagesLoaded()) GuessParameters();
  rviewUI->UpdateRegistrationControlWindow();
}

void Fl_RViewUI::cb_registrationParameter(Fl_Widget *, void *)
{
  int i;

  params.similarity       = (rviewUI->similarityMeasure->value() == 1) ? SSD : NMI;
  params.bins             = round(rviewUI->numberOfBins->value());
  params.iterations       = round(rviewUI->numberOfIterations->value());
  params.maxStep          = rviewUI->maxStepLength->value();
  params.minStep          = rviewUI->minStepLength->value();
  params.targetBlurring   = rviewUI->targetBlurring->value();
  params.targetResolution = rviewUI->targetResolution->value();
  params.sourceBlurring   = rviewUI->sourceBlurring->value();
  params.sourceResolution = rviewUI->sourceResolution->value();
  params.spacing          = rviewUI->controlPointSpacing->value();
  params.lambda1          = rviewUI->bendingEnergy->value();

  // Leftmost padding position means "no padding"
  if (rviewUI->targetPadding->value() <= rviewUI->targetPadding->minimum()) {
    params.targetPadding = MIN_GREY;
  } else {
    params.targetPadding = rviewUI->targetPadding->value();
  }

  for (i = 0; i < 5; i++) {
    if (rviewUI->level[i]->value()) params.levels = i + 1;
  }
}

void Fl_RViewUI::cb_loadParameter(Fl_Button *, void *)
{
  if (!ImagesLoaded()) {
    fl_alert("Load a target and a source image first");
    return;
  }

  char *filename = fl_file_chooser("Load registration parameters", "*", "");
  if (filename == NULL) return;

  irtkRealImage target, source;
  GetRegistrationImages(target, source);
  irtkTransformation *output = InitialTransformation(params.type);
  RegistrationAccess *access = SetUpRegistration(target, source, output);
  access->Put(params);
  access->Registration()->irtkImageRegistration2::Read(filename);
  access->Get(params);
  delete access;
  delete output;

  rviewUI->UpdateRegistrationControlWindow();
}

void Fl_RViewUI::cb_saveParameter(Fl_Button *, void *)
{
  if (!ImagesLoaded()) {
    fl_alert("Load a target and a source image first");
    return;
  }

  char *filename = fl_file_chooser("Save registration parameters", "*", "");
  if (filename == NULL) return;

  irtkRealImage target, source;
  GetRegistrationImages(target, source);
  irtkTransformation *output = InitialTransformation(params.type);
  RegistrationAccess *access = SetUpRegistration(target, source, output);
  access->Put(params);
  access->Registration()->irtkImageRegistration2::Write(filename);
  delete access;
  delete output;
}

void Fl_RViewUI::cb_startRegistration(Fl_Button *, void *)
{
  if (job != NULL) return;

  if (!ImagesLoaded()) {
    fl_alert("Load a target and a source image first");
    return;
  }

  if (!params.guessed) GuessParameters();

  rviewUI->registrationStatus->value("Preparing images ...");
  Fl::check();

  // The job owns copies of everything, so the viewer can be used while it runs
  job = new RegistrationJob;
  job->type = params.type;
  job->done = false;
  GetRegistrationImages(job->target, job->source);
  job->transformation = InitialTransformation(job->type);
  job->access = SetUpRegistration(job->target, job->source, job->transformation);
  job->access->Put(params);

  SetRunning(true);
  rviewUI->registrationStatus->value("Running ... (progress is printed in the terminal)");

  job->thread = std::thread([]() {
    job->access->Registration()->Run();
    job->done = true;
  });

  Fl::add_timeout(0.25, cb_registrationPoll);
}

void Fl_RViewUI::cb_registrationPoll(void *)
{
  int i;
  char filename[1200], status[1300];

  if (job == NULL) return;

  if (!job->done) {
    Fl::repeat_timeout(0.25, cb_registrationPoll);
    return;
  }

  job->thread.join();

  // Save the result and load it the same way as "Load" on the Transform tab
  registrationCount++;
  snprintf(filename, sizeof(filename), "%s/%s_%d.dof", OutputDirectory(), registrationTypeName[job->type], registrationCount);
  job->transformation->irtkTransformation::Write(filename);

  delete job->access;
  delete job->transformation;
  delete job;
  job = NULL;

  rview->ReadTransformation(filename);
  rview->SetSourceTransformApply(true);

  rviewUI->transformationBrowser->add(filename);
  rviewUI->transformationBrowser->deselect();
  rviewUI->transformationBrowser->select(rviewUI->transformationBrowser->size());
  rviewUI->info_trans_filename->value(filename);

  irtkTransformation *transform = rview->GetTransformation();
  if (dynamic_cast<irtkFreeFormTransformation *>(transform) == NULL) {
    for (i = 0; i < transform->NumberOfDOFs(); i++) {
      rviewUI->transformationValuator[i]->value(transform->Get(i));
    }
  }

  SetRunning(false);
  snprintf(status, sizeof(status), "Done: %s", filename);
  rviewUI->registrationStatus->value(status);

  rview->Update();
  viewer->redraw();
  rviewUI->update();

  fl_message("Registration has finished.\n\nThe result is applied to the source image and saved as\n%s\n\nUse Save on the Transform/Fusion tab to keep it somewhere else.", filename);
}

void Fl_RViewUI::cb_guessRegistrationParameter(Fl_Button *, void *)
{
  if (!ImagesLoaded()) {
    fl_alert("Load a target and a source image first");
    return;
  }
  GuessParameters();
  rviewUI->UpdateRegistrationControlWindow();
}

void Fl_RViewUI::UpdateRegistrationControlWindow()
{
  int i;
  double padMin, padMax;

  // Guess parameters once both images are available
  if (!params.guessed && (job == NULL) && ImagesLoaded()) GuessParameters();

  registrationType->value(params.type);
  similarityMeasure->value((params.similarity == SSD) ? 1 : 0);
  numberOfBins->value(params.bins);
  numberOfIterations->value(params.iterations);
  maxStepLength->value(params.maxStep);
  minStepLength->value(params.minStep);

  // Let the sliders cover the current values
  targetBlurring->maximum(max(5.0, 2 * params.targetBlurring));
  sourceBlurring->maximum(max(5.0, 2 * params.sourceBlurring));
  targetResolution->maximum(max(5.0, 2 * params.targetResolution));
  sourceResolution->maximum(max(5.0, 2 * params.sourceResolution));
  controlPointSpacing->maximum(max(50.0, 2 * params.spacing));
  targetBlurring->value(params.targetBlurring);
  targetResolution->value(params.targetResolution);
  sourceBlurring->value(params.sourceBlurring);
  sourceResolution->value(params.sourceResolution);
  controlPointSpacing->value(params.spacing);
  bendingEnergy->value(params.lambda1);

  if (rview->GetTarget()->IsEmpty() != true) {
    padMin = rview->GetTargetMin() - 1;
    padMax = rview->GetTargetMax();
  } else {
    padMin = -1;
    padMax = 1;
  }
  targetPadding->minimum(padMin);
  targetPadding->maximum(padMax);
  if (params.targetPadding <= padMin) {
    targetPadding->value(padMin);
  } else if (params.targetPadding > padMax) {
    targetPadding->value(padMax);
  } else {
    targetPadding->value(params.targetPadding);
  }

  for (i = 0; i < 5; i++) level[i]->value(params.levels == i + 1);

  if (params.type == Reg_NonRigid) {
    controlPointSpacing->activate();
    bendingEnergy->activate();
  } else {
    controlPointSpacing->deactivate();
    bendingEnergy->deactivate();
  }
}

static Fl_Value_Slider *NewRegistrationSlider(int y, const char *label, double step)
{
  Fl_Value_Slider *o = new Fl_Value_Slider(15, y, 360, 18, label);
  o->type(5);
  o->box(FL_EMBOSSED_BOX);
  o->step(step);
  o->minimum(0);
  o->maximum(5);
  o->align(FL_ALIGN_TOP_LEFT);
  o->labelsize(12);
  o->callback((Fl_Callback *)Fl_RViewUI::cb_registrationParameter);
  return o;
}

static Fl_Counter *NewRegistrationCounter(int y, const char *label, double minimum, double maximum, double step, double lstep)
{
  Fl_Counter *o = new Fl_Counter(175, y, 200, 24, label);
  o->align(FL_ALIGN_LEFT);
  o->labelsize(12);
  o->minimum(minimum);
  o->maximum(maximum);
  o->step(step);
  o->lstep(lstep);
  o->callback((Fl_Callback *)Fl_RViewUI::cb_registrationParameter);
  return o;
}

void Fl_RViewUI::InitializeRegistrationControlWindow()
{
  int i;
  static const char *levelLabel[5] = { "1", "2", "3", "4", "5" };

  {
    // Registration type, similarity measure and resolution levels
    Fl_Group *o = new Fl_Group(5, 35, 380, 100);
    o->box(FL_ENGRAVED_BOX);
    {
      Fl_Choice *o = registrationType = new Fl_Choice(175, 42, 200, 24, "Registration type:");
      o->labelsize(12);
      o->menu(menu_registrationType);
      o->callback((Fl_Callback *)cb_registrationType);
    }
    {
      Fl_Choice *o = similarityMeasure = new Fl_Choice(175, 72, 200, 24, "Similarity measure:");
      o->labelsize(12);
      o->menu(menu_similarityMeasure);
      o->callback((Fl_Callback *)cb_registrationParameter);
    }
    {
      Fl_Box *o = new Fl_Box(15, 102, 100, 24, "Levels:");
      o->labelsize(12);
      o->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
    for (i = 0; i < 5; i++) {
      Fl_Check_Button *o = level[i] = new Fl_Check_Button(175 + 40 * i, 102, 40, 24, levelLabel[i]);
      o->down_box(FL_DIAMOND_DOWN_BOX);
      o->type(FL_RADIO_BUTTON);
      o->labelsize(12);
      o->callback((Fl_Callback *)cb_registrationParameter);
    }
    o->end();
  }
  {
    // Optimisation parameters
    Fl_Group *o = new Fl_Group(5, 140, 380, 130);
    o->box(FL_ENGRAVED_BOX);
    numberOfBins       = NewRegistrationCounter(147, "No. of bins:", 2, 1000, 1, 10);
    numberOfIterations = NewRegistrationCounter(177, "No. of iterations:", 1, 1000, 1, 10);
    maxStepLength      = NewRegistrationCounter(207, "Max. step length:", 0.01, 20, 0.01, 0.5);
    minStepLength      = NewRegistrationCounter(237, "Min. step length:", 0.001, 5, 0.001, 0.01);
    o->end();
  }
  {
    // Image pre-processing parameters (values for level 1, doubled per level)
    Fl_Group *o = new Fl_Group(5, 275, 380, 215);
    o->box(FL_ENGRAVED_BOX);
    targetBlurring   = NewRegistrationSlider(295, "Target blurring (mm, level 1):", 0.05);
    targetResolution = NewRegistrationSlider(335, "Target resolution (mm, level 1):", 0.05);
    sourceBlurring   = NewRegistrationSlider(375, "Source blurring (mm, level 1):", 0.05);
    sourceResolution = NewRegistrationSlider(415, "Source resolution (mm, level 1):", 0.05);
    targetPadding    = NewRegistrationSlider(455, "Target padding (ignore intensities below; leftmost = off):", 1);
    o->end();
  }
  {
    // Non-rigid parameters
    Fl_Group *o = new Fl_Group(5, 495, 380, 75);
    o->box(FL_ENGRAVED_BOX);
    controlPointSpacing = NewRegistrationSlider(515, "Control point spacing (mm, non-rigid only):", 0.5);
    {
      Fl_Value_Input *o = bendingEnergy = new Fl_Value_Input(250, 540, 125, 24, "Bending energy weight (non-rigid):");
      o->labelsize(12);
      o->minimum(0);
      o->maximum(1);
      o->step(0.0001);
      o->callback((Fl_Callback *)cb_registrationParameter);
    }
    o->end();
  }
  {
    // Controls
    Fl_Group *o = new Fl_Group(5, 575, 380, 80);
    o->box(FL_ENGRAVED_BOX);
    {
      Fl_Button *o = startRegistrationButton = new Fl_Return_Button(12, 582, 85, 28, "Start");
      o->callback((Fl_Callback *)cb_startRegistration);
    }
    {
      Fl_Button *o = guessRegistrationButton = new Fl_Button(104, 582, 85, 28, "Guess");
      o->tooltip("Reset all parameters to the defaults for these images");
      o->callback((Fl_Callback *)cb_guessRegistrationParameter);
    }
    {
      Fl_Button *o = loadParameterButton = new Fl_Button(196, 582, 85, 28, "Load ...");
      o->tooltip("Load a registration parameter file (as used with -parin)");
      o->callback((Fl_Callback *)cb_loadParameter);
    }
    {
      Fl_Button *o = saveParameterButton = new Fl_Button(288, 582, 85, 28, "Save ...");
      o->tooltip("Save the parameters as a file usable with -parin");
      o->callback((Fl_Callback *)cb_saveParameter);
    }
    {
      Fl_Output *o = registrationStatus = new Fl_Output(12, 618, 361, 26);
      o->textsize(11);
      o->value("Ready");
    }
    o->end();
  }
}

#endif
