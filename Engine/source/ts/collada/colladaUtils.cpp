//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include <algorithm>
#include "console/console.h"
#include "gfx/bitmap/gBitmap.h"
#include "ts/collada/colladaUtils.h"
#include "materials/matInstance.h"

//special handling for export classes
#include "persistence/taml/fsTinyXml.h"
#include "T3D/convexShape.h"

using namespace ColladaUtils;

#define MAX_PATH_LENGTH 256

// Helper macro to create Collada elements
#define CREATE_ELEMENT(container, name, type)   \
   type* name = daeSafeCast<type>(container->createAndPlace(#name));

ColladaUtils::ImportOptions& ColladaUtils::getOptions()
{
   static ImportOptions options;
   return options;
}

//------------------------------------------------------------------------------
// Utility functions

// Convert a transform from the Collada model coordinate system to the DTS coordinate
// system
void ColladaUtils::convertTransform(MatrixF& mat)
{
   MatrixF rot(true);

   switch (ColladaUtils::getOptions().upAxis)
   {
      case UPAXISTYPE_X_UP:
         // rotate 90 around Y-axis, then 90 around Z-axis
	      rot(0,0) = 0.0f;  rot(1,0) = 1.0f;
	      rot(1,1) = 0.0f;	rot(2,1) = 1.0f;
	      rot(0,2) = 1.0f;	rot(2,2) = 0.0f;

	      // pre-multiply the transform by the rotation matrix
	      mat.mulL(rot);
         break;

      case UPAXISTYPE_Y_UP:
         // rotate 180 around Y-axis, then 90 around X-axis
	      rot(0,0) = -1.0f;
	      rot(1,1) = 0.0f;	rot(2,1) = 1.0f;
	      rot(1,2) = 1.0f;	rot(2,2) = 0.0f;

	      // pre-multiply the transform by the rotation matrix
	      mat.mulL(rot);
         break;

      case UPAXISTYPE_Z_UP:
      default:
         // nothing to do
         break;
   }
}

/// Find the COMMON profile element in an effect
const domProfile_COMMON* ColladaUtils::findEffectCommonProfile(const domEffect* effect)
{
   if (effect) {
      // Find the COMMON profile
      const domFx_profile_abstract_Array& profiles = effect->getFx_profile_abstract_array();
      for (S32 iProfile = 0; iProfile < profiles.getCount(); iProfile++) {
         if (profiles[iProfile]->getElementType() == COLLADA_TYPE::PROFILE_COMMON)
            return daeSafeCast<domProfile_COMMON>(profiles[iProfile]);
      }
   }
   return NULL;
}

/// Find the <diffuse> element in the COMMON profile of an effect
const domCommon_color_or_texture_type_complexType* ColladaUtils::findEffectDiffuse(const domEffect* effect)
{
   const domProfile_COMMON* profile = findEffectCommonProfile(effect);
   if (profile) {

      if (profile->getTechnique()->getLambert())
         return profile->getTechnique()->getLambert()->getDiffuse();
      else if (profile->getTechnique()->getPhong())
         return profile->getTechnique()->getPhong()->getDiffuse();
      else if (profile->getTechnique()->getBlinn())
         return profile->getTechnique()->getBlinn()->getDiffuse();
   }

   return NULL;
}

/// Find the <specular> element in the COMMON profile of an effect
const domCommon_color_or_texture_type_complexType* ColladaUtils::findEffectSpecular(const domEffect* effect)
{
   const domProfile_COMMON* profile = findEffectCommonProfile(effect);
   if (profile) {

      if (profile->getTechnique()->getLambert())
         return NULL;      // no <specular> element for Lambert shader
      else if (profile->getTechnique()->getPhong())
         return profile->getTechnique()->getPhong()->getSpecular();
      else if (profile->getTechnique()->getBlinn())
         return profile->getTechnique()->getBlinn()->getSpecular();
   }

   return NULL;
}

const domFx_sampler2D_common_complexType* ColladaUtils::getTextureSampler(const domEffect* effect,
                                                                          const domCommon_color_or_texture_type_complexType* texture)
{
   // <texture texture="new_param_SID">.<newparam>.<sampler2D>
   if (texture) {
      const domCommon_color_or_texture_type_complexType::domTexture* domTex = texture->getTexture();
      if (domTex && domTex->getTexture()) {
         daeSIDResolver resolver(const_cast<domEffect*>(effect), domTex->getTexture());
         const domCommon_newparam_type* param = daeSafeCast<domCommon_newparam_type>(resolver.getElement());
         if (param)
            return param->getSampler2D();
      }
   }

   return NULL;
}

String ColladaUtils::getSamplerImagePath(const domEffect* effect,
                                         const domFx_sampler2D_common_complexType* sampler2D)
{
   // <sampler2D>.<source>.<newparam>.<surface>.<init_from>.<image>.<init_from>
   const domProfile_COMMON* profile = findEffectCommonProfile(effect);
   if (profile && sampler2D && sampler2D->getSource()) {

      // Resolve the SID to get the <surface> param
      daeSIDResolver resolver(const_cast<domProfile_COMMON*>(profile), sampler2D->getSource()->getValue());
      domCommon_newparam_type* surfaceParam = daeSafeCast<domCommon_newparam_type>(resolver.getElement());

      // Get the surface <init_from> element
      if (surfaceParam && surfaceParam->getSurface()) {

         const domFx_surface_init_common* surfaceInit = surfaceParam->getSurface()->getFx_surface_init_common();
         if (surfaceInit && surfaceInit->getInit_from_array().getCount()) {
            // Resolve the ID to get the <image>, then read the texture path
            xsIDREF& idRef = surfaceInit->getInit_from_array()[0]->getValue();
            const domImage* image = daeSafeCast<domImage>(idRef.getElement());
            if (image && image->getInit_from())
               return resolveImagePath(image);
         }
      }
   }

   return "";
}

// Resolve image path into something we can use.
String ColladaUtils::resolveImagePath(const domImage* image)
{
   // 1. If the URI string contains an absolute path, use it if
   //    it is inside the Torque folder, otherwise force textures
   //    to be in the same folder as the shape.
   // 2. If the URI string contains a relative path, append it
   //    to the shape path (since materials.tscript cannot handle
   //    relative paths).

   Torque::Path imagePath;
   String imageStr(image->getInit_from()->getValue().originalStr().c_str());

   // Trim leading "file://"
   if (imageStr.compare("file://", 7) == 0)
      imageStr.erase(0, 7);

   // Trim leading slash from absolute windows paths. eg. /D:/
   if ((imageStr.compare("/", 1) == 0) && (imageStr.find(':') == 2))
      imageStr.erase(0, 1);

   // Replace %20 with space
	imageStr.replace("%20", " ");

   if (Platform::isFullPath(imageStr))
   {
      // Absolute path => check for outside the Torque game folder
      imagePath = String( Platform::makeRelativePathName(imageStr, Platform::getMainDotCsDir()) );
      if ( !imagePath.getRoot().isEmpty()       ||    // different drive (eg. C:/ vs D:/)
           (imagePath.getPath().find("/") == 0) ||    // different OS (eg. /home vs C:/home)
           (imagePath.getPath().find("../") == 0) )   // same drive, outside Torque game folder
      {
         // Force these to the shape folder
         imagePath.setRoot("");
         imagePath.setPath("");
      }
   }
   else
   {
      // Relative path => prepend with shape path
      Torque::Path tempPath(imageStr);
      imagePath = TSShapeLoader::getShapePath();
      imagePath.appendPath(tempPath);
      imagePath.setFileName(tempPath.getFileName());
   }

   // No need to specify the path if it is in the same folder as the model
   if (imagePath.getPath() == TSShapeLoader::getShapePath().getPath())
      imagePath.setPath("");

   // Don't care about the extension
   imagePath.setExtension("");

   return imagePath.getFullPath();
}

//-----------------------------------------------------------------------------
// Construct the appropriate child class
BasePrimitive* BasePrimitive::get(const daeElement* element)
{
   switch (element->getElementType()) {
      case COLLADA_TYPE::TRIANGLES: return new ColladaPrimitive<domTriangles>(element);
      case COLLADA_TYPE::TRISTRIPS: return new ColladaPrimitive<domTristrips>(element);
      case COLLADA_TYPE::TRIFANS:   return new ColladaPrimitive<domTrifans>(element);
      case COLLADA_TYPE::POLYGONS:  return new ColladaPrimitive<domPolygons>(element);
      case COLLADA_TYPE::POLYLIST:  return new ColladaPrimitive<domPolylist>(element);
      default:                      return 0;
   }
}

//------------------------------------------------------------------------------
// Collada animation curves

/// Determine which elements are being targeted
void AnimData::parseTargetString(const char* target, S32 fullCount, const char* elements[])
{
   // Assume targeting all elements at offset 0
   targetValueCount = fullCount;
   targetValueOffset = 0;

   // Check for array syntax: (n) or (n)(m)
   if (const char* p = dStrchr(target, '(')) {
      S32 indN, indM;
      if (dSscanf(p, "(%d)(%d)", &indN, &indM) == 2) {
         targetValueOffset = (indN * 4) + indM;   // @todo: 4x4 matrix only
         targetValueCount = 1;
      }
      else if (dSscanf(p, "(%d)", &indN) == 1) {
         targetValueOffset = indN;
         targetValueCount = 1;
      }
   }
   else if (const char* p2 = dStrrchr(target, '.')) {
      // Check for named elements
      for (S32 iElem = 0; elements[iElem][0] != 0; iElem++) {
         if (!String::compare(p2, elements[iElem])) {
            targetValueOffset = iElem;
            targetValueCount = 1;
            break;
         }
      }
   }
}

/// Solve the cubic spline B(s) = param for s
F32 AnimData::invertParamCubic(F32 param, F32 x0, F32 x1, F32 x2, F32 x3) const
{
   const F64 INVERTPARAMCUBIC_TOL         = 1.0e-09;
   const F64 INVERTPARAMCUBIC_SMALLERTOL  = 1.0e-20;
   const F64 INVERTPARAMCUBIC_MAXIT       = 100;

   // check input value for outside range
   if ((param - x0) < INVERTPARAMCUBIC_SMALLERTOL)
      return 0.0f;
   else if ((x3 - param) < INVERTPARAMCUBIC_SMALLERTOL)
      return 1.0f;

   U32 iterations = 0;

   // de Casteljau Subdivision.
   F32 u = 0.0f;
   F32 v = 1.0f;

   while (iterations < INVERTPARAMCUBIC_MAXIT) {
      F64 a = (x0 + x1)*0.5f;
      F64 b = (x1 + x2)*0.5f;
      F64 c = (x2 + x3)*0.5f;
      F64 d = (a + b)*0.5f;
      F64 e = (b + c)*0.5f;
      F64 f = (d + e)*0.5f;

      if (mFabs(f - param) < INVERTPARAMCUBIC_TOL)
         break;

      if (f < param) {
         x0 = f;
         x1 = e;
         x2 = c;
         u = (u + v)*0.5f;
      }
      else {
         x1 = a;
         x2 = d;
         x3 = f;
         v = (u + v)*0.5f;
      }
      iterations++;
   }

   return mClampF((u+v)*0.5f, 0.0f, 1.0f);
}

/// Get the interpolated value at time 't'
void AnimData::interpValue(F32 t, U32 offset, double* value) const
{
   // handle degenerate animation data
   if (input.size() == 0)
   {
      *value = 0.0f;
      return;
   }
   else if (input.size() == 1)
   {
      *value = output.getStringArrayData(0)[offset];
      return;
   }

   // clamp time to valid range
   F32 curveStart = input.getFloatValue(0);
   F32 curveEnd = input.getFloatValue(input.size()-1);
   t = mClampF(t, curveStart, curveEnd);

   // find the index of the input keyframe BEFORE 't'
   S32 index;
   for (index = 0; index < input.size()-2; index++) {
      if (input.getFloatValue(index + 1) > t)
         break;
   }

   // get the data for the two control points either side of 't'
   Point2F v0;
   v0.x = input.getFloatValue(index);
   v0.y = output.getStringArrayData(index)[offset];

   Point2F v3;
   v3.x = input.getFloatValue(index + 1);
   v3.y = output.getStringArrayData(index + 1)[offset];

   // If spline interpolation is specified but the tangents are not available,
   // default to LINEAR.
   const char* interp_method = interpolation.getStringValue(index);
   if (dStrEqual(interp_method, "BEZIER") ||
       dStrEqual(interp_method, "HERMITE") ||
       dStrEqual(interp_method, "CARDINAL")) {

      const double* inArray = inTangent.getStringArrayData(index + 1);
      const double* outArray = outTangent.getStringArrayData(index);
      if (!inArray || !outArray)
         interp_method = "LINEAR";
   }

   if (dStrEqual(interp_method, "STEP")) {
      // STEP interpolation
      *value = v0.y;
   }
   else if (dStrEqual(interp_method, "BEZIER") ||
            dStrEqual(interp_method, "HERMITE") ||
            dStrEqual(interp_method, "CARDINAL") ||
            dStrEqual(interp_method, "BSPLINE"))
   {
      // Cubic spline interpolation. The only difference between the 4 supported
      // forms is in the calculation of the other 2 control points:
      // BEZIER: control points are specified explicitly
      // HERMITE: tangents are specified, need to offset to get the control points
      // CARDINAL: (baked) tangents are specified, need to offset to get the control points
      // BSPLINE: control points are based on previous and next points

      // Get the 2 extra control points
      Point2F v1, v2;

      if (dStrEqual(interp_method, "BSPLINE")) {
         // v0 and v3 are the center points => need to
         // get the control points before and after them
         v1 = v0;
         v2 = v3;

         if (index > 0) {
            v0.x = input.getFloatValue(index-1);
            v0.y = output.getStringArrayData(index-1)[offset];
         }
         else {
            // mirror P1 through P0
            v0 = v1 + (v1 - v2);
         }

         if (index < (input.size()-2)) {
            v3.x = input.getFloatValue(index+2);
            v3.y = output.getStringArrayData(index+2)[offset];
         }
         else {
            // mirror P0 through P1
            v3 = v2 + (v2 - v1);
         }
      }
      else {
         const double* inArray = inTangent.getStringArrayData(index + 1);
         const double* outArray = outTangent.getStringArrayData(index);

         if (output.stride() == inTangent.stride()) {
            // This degenerate form (1D control points) does 2 things wrong:
            // 1) it does not specify the key (time) value
            // 2) the control point is specified as a tangent for both bezier and hermite
            // => interpolate to get the key values, and offset the tangent values
            v1.set((v0.x*2 + v3.x)/3, v0.y + outArray[offset]);
            v2.set((v0.x + v3.x*2)/3, v3.y - inArray[offset]);
         }
         else {
            // the expected form (2D control points)
            v1.set(outArray[offset*2], outArray[offset*2+1]);
            v2.set(inArray[offset*2], inArray[offset*2+1]);

            // if this is a hermite or cardinal spline, treat the values as tangents
            if (dStrEqual(interp_method, "HERMITE") || dStrEqual(interp_method, "CARDINAL")) {
               v1.set(v0.x + v1.x, v3.y - v1.y);
               v2.set(v0.x + v2.x, v3.x - v2.y);
            }
         }
      }

      // find 's' that gives the desired 't' value
      F32 s = invertParamCubic(t, v0.x, v1.x, v2.x, v3.x);

      // Calculate the output value using Bernstein evaluation and the
      // computed 's' value
      F32 c = 3.0f*(v1.y - v0.y);
      F32 e = 3.0f*(v2.y - v1.y);
      *value = (((v3.y - v0.y - e)*s + e - c)*s + c)*s + v0.y;
   }
   else {
      // default to LINEAR interpolation
      F32 s = mClampF((t - v0.x) / (v3.x - v0.x), 0.0f, 1.0f);
      *value = v0.y + (v3.y - v0.y) * s;
   }
}

void AnimData::interpValue(F32 t, U32 offset, const char** value) const
{
   if (input.size() == 0)
      *value = "";
   else if (input.size() == 1)
      *value = output.getStringValue(0);
   else
   {
      // clamp time to valid range
      F32 curveStart = input.getFloatValue(0);
      F32 curveEnd = input.getFloatValue(input.size()-1);
      t = mClampF(t, curveStart, curveEnd);

      // find the index of the input keyframe BEFORE 't'
      S32 index;
      for (index = 0; index < input.size()-2; index++) {
         if (input.getFloatValue(index + 1) > t)
            break;
      }

      // String values only support STEP interpolation, so just get the
      // value at the input keyframe
      *value = output.getStringValue(index);
   }
}

//------------------------------------------------------------------------------
// Collada document conditioners

static void conditioner_fixupTextureSIDs(domCOLLADA* root)
{
   for (S32 iLib = 0; iLib < root->getLibrary_effects_array().getCount(); iLib++) {
      domLibrary_effects* lib = root->getLibrary_effects_array()[iLib];
      for (S32 iEffect = 0; iEffect < lib->getEffect_array().getCount(); iEffect++) {
         domEffect* effect = lib->getEffect_array()[iEffect];
         const domCommon_color_or_texture_type_complexType* diffuse = findEffectDiffuse(effect);
         if (!diffuse || !diffuse->getTexture())
            continue;

         // Resolve the SID => if it is an <image>, add <sampler2D> and
         // <surface> elements to conform to the Collada spec.
         const char *image_sid = diffuse->getTexture()->getTexture();
         daeSIDResolver resolver(effect, image_sid);
         if (!daeSafeCast<domImage>(resolver.getElement()))
            continue;

         daeErrorHandler::get()->handleWarning(avar("Fixup %s <diffuse>.<texture> "
            "pointing at <image> instead of <sampler2D>", effect->getID()));

         // Generate SIDs for the new sampler2D and surface elements
         std::string sampler_sid(std::string(image_sid) + "-sampler");
         std::string surface_sid(std::string(image_sid) + "-surface");

         domProfile_COMMON* profile = const_cast<domProfile_COMMON*>(findEffectCommonProfile(effect));

         // Create <newparam>.<sampler2D>.<source>
         {
            CREATE_ELEMENT(profile,    newparam,   domCommon_newparam_type)
            CREATE_ELEMENT(newparam,   sampler2D,  domFx_sampler2D_common)
            CREATE_ELEMENT(sampler2D,  source,     domFx_sampler2D_common_complexType::domSource)

            newparam->setSid(sampler_sid.c_str());
            source->setValue(surface_sid.c_str());
         }

         // Create <newparam>.<surface>.<init_from>
         {
            CREATE_ELEMENT(profile,    newparam,   domCommon_newparam_type)
            CREATE_ELEMENT(newparam,   surface,    domFx_surface_common)
            CREATE_ELEMENT(surface,    init_from,  domFx_surface_init_from_common)
            CREATE_ELEMENT(surface,    format,     domFx_surface_common_complexType::domFormat)

            newparam->setSid(surface_sid.c_str());
            surface->setType(FX_SURFACE_TYPE_ENUM_2D);
            format->setValue("A8R8G8B8");
            init_from->setValue(image_sid);
         }

         // Store sampler2D sid in the <diffuse>.<texture> "texture" attribute
         diffuse->getTexture()->setTexture(sampler_sid.c_str());
      }
   }
}

static void conditioner_fixupImageURIs(domCOLLADA* root)
{
   for (S32 iLib = 0; iLib < root->getLibrary_images_array().getCount(); iLib++) {
      domLibrary_images* lib = root->getLibrary_images_array()[iLib];
      for (S32 iImage = 0; iImage < lib->getImage_array().getCount(); iImage++) {
         domImage* image = lib->getImage_array()[iImage];
         if (image->getInit_from()) {
            xsAnyURI& uri = image->getInit_from()->getValue();

            // Replace '\' with '/'
            if (uri.originalStr().find("\\") != std::string::npos) {
               daeErrorHandler::get()->handleWarning(avar("Fixup invalid URI "
                  "in %s: \"%s\"", image->getID(), uri.originalStr().c_str()));

               std::string str(uri.originalStr());
               std::replace(str.begin(), str.end(), '\\', '/');
               uri.set(str);
            }

            // Detect file://texture.jpg => this is an invalid URI and will
            // not be parsed correctly
            if (uri.scheme() == "file" &&
               uri.pathFile().empty() &&
               !uri.authority().empty()) {
               daeErrorHandler::get()->handleWarning(avar("Fixup invalid URI "
                  "in %s: \"%s\"", image->getID(), uri.originalStr().c_str()));

               uri.set(uri.authority());
            }
         }
      }
   }
}

static void conditioner_fixupTransparency(domCOLLADA* root)
{
   // Transparency is another example of something simple made complicated by
   // Collada. There are two (optional) elements that determine transparency:
   //
   // <transparent>: a color
   // <transparency>: a percentage applied to the color values
   //
   // Additionally, <transparent> has an optional "opaque" attribute that changes
   // the way transparency is determined. If set to A_ONE (the default), only the
   // alpha value of the transparent color is used, and a value of "1" means fully
   // opaque. If set to RGB_ZERO, only the RGB values of transparent are used, and
   // a value of "0" means fully opaque.
   //
   // To further complicate matters, Google Sketchup (all versions) and FeelingSoftware
   // ColladaMax (pre 3.03) export materials with the transparency element inverted
   // (1-transparency)

   // Get the <authoring_tool> string
   const char *authoringTool = "";
   if (const domAsset* asset = root->getAsset()) {
      for (S32 iContrib = 0; iContrib < asset->getContributor_array().getCount(); iContrib++) {
         const domAsset::domContributor* contrib = asset->getContributor_array()[iContrib];
         if (contrib->getAuthoring_tool()) {
            authoringTool = contrib->getAuthoring_tool()->getValue();
            break;
         }
      }
   }

   // Check for a match with the known problem-tools
   bool invertTransparency = false;
   const char *toolNames[] = {   "FBX COLLADA exporter", "Google SketchUp",
                                 "Illusoft Collada Exporter", "FCollada" };
   for (S32 iName = 0; iName < (sizeof(toolNames)/sizeof(toolNames[0])); iName++) {
      if (dStrstr(authoringTool, toolNames[iName])) {
         invertTransparency = true;
         break;
      }
   }

   if (!invertTransparency)
      return;

   // Invert transparency as required for each effect
   for (S32 iLib = 0; iLib < root->getLibrary_effects_array().getCount(); iLib++) {
      domLibrary_effects* lib = root->getLibrary_effects_array()[iLib];
      for (S32 iEffect = 0; iEffect < lib->getEffect_array().getCount(); iEffect++) {
         domEffect* effect = lib->getEffect_array()[iEffect];

         // Find the common profile
         const domProfile_COMMON* commonProfile = findEffectCommonProfile(effect);
         if (!commonProfile)
            continue;

         domCommon_transparent_type* transparent = 0;
         if (commonProfile->getTechnique()->getConstant())
            transparent = commonProfile->getTechnique()->getConstant()->getTransparent();
         else if (commonProfile->getTechnique()->getLambert())
            transparent = commonProfile->getTechnique()->getLambert()->getTransparent();
         else if (commonProfile->getTechnique()->getPhong())
            transparent = commonProfile->getTechnique()->getPhong()->getTransparent();
         else if (commonProfile->getTechnique()->getBlinn())
            transparent = commonProfile->getTechnique()->getBlinn()->getTransparent();

         if (!transparent)
            continue;

         // If the shader "opaque" attribute is not specified, set it to
         // RGB_ZERO (the opposite of the Collada default), as this is what
         // the bad exporter tools seem to assume.
         if (!transparent->isAttributeSet("opaque")) {

            daeErrorHandler::get()->handleWarning(avar("Setting <transparent> "
               "\"opaque\" attribute to RGB_ZERO for %s <effect>", effect->getID()));

            transparent->setOpaque(FX_OPAQUE_ENUM_RGB_ZERO);
         }
      }
   }
}

static void conditioner_checkBindShapeMatrix(domCOLLADA* root)
{
   for (S32 iLib = 0; iLib < root->getLibrary_controllers_array().getCount(); iLib++) {
      domLibrary_controllers* lib = root->getLibrary_controllers_array().get(iLib);
      for (S32 iCon = 0; iCon < lib->getController_array().getCount(); iCon++) {
         domController* con = lib->getController_array().get(iCon);
         if (con->getSkin() && con->getSkin()->getBind_shape_matrix()) {

            MatrixF mat = vecToMatrixF<domMatrix>(con->getSkin()->getBind_shape_matrix()->getValue());
            if (!mat.fullInverse()) {
               daeErrorHandler::get()->handleWarning(avar("<bind_shape_matrix> "
                  "in %s <controller> is not invertible (may cause problems with "
                  "skinning)", con->getID()));
            }
         }
      }
   }
}

static void conditioner_fixupVertexWeightJoints(domCOLLADA* root)
{
   for (S32 iLib = 0; iLib < root->getLibrary_controllers_array().getCount(); iLib++) {
      domLibrary_controllers* lib = root->getLibrary_controllers_array().get(iLib);
      for (S32 iCon = 0; iCon < lib->getController_array().getCount(); iCon++) {
         domController* con = lib->getController_array().get(iCon);
         if (con->getSkin() && con->getSkin()->getVertex_weights())
         {
            domInputLocalOffset_Array& vw_inputs = con->getSkin()->getVertex_weights()->getInput_array();
            for (S32 vInput = 0; vInput < vw_inputs.getCount(); vInput++) {

               domInputLocalOffset *vw_input = vw_inputs.get(vInput);
               if (dStrEqual(vw_input->getSemantic(), "JOINT")) {

                  // Check if this input points at a float array (bad)
                  domSource* vw_source = daeSafeCast<domSource>(vw_input->getSource().getElement());
                  if (vw_source->getFloat_array()) {

                     // Copy the value from the <joints> JOINTS input instead
                     domInputLocal_Array& joint_inputs = con->getSkin()->getJoints()->getInput_array();
                     for (S32 jInput = 0; jInput < joint_inputs.getCount(); jInput++) {

                        domInputLocal *joint_input = joint_inputs.get(jInput);
                        if (dStrEqual(joint_input->getSemantic(), "JOINT")) {
                           vw_input->setSource(joint_input->getSource());
                           break;
                        }
                     }
                  }
               }
            }
         }
      }
   }
}

static void conditioner_createDefaultClip(domCOLLADA* root)
{
   // Check if the document has any <animation_clip>s
   for (S32 iLib = 0; iLib < root->getLibrary_animation_clips_array().getCount(); iLib++) {
      if (root->getLibrary_animation_clips_array()[iLib]->getAnimation_clip_array().getCount())
         return;
   }

   // Get all top-level <animation>s into an array
   domAnimation_Array animations;
   for (S32 iAnimLib = 0; iAnimLib < root->getLibrary_animations_array().getCount(); iAnimLib++) {
      const domLibrary_animations* libraryAnims = root->getLibrary_animations_array()[iAnimLib];
      for (S32 iAnim = 0; iAnim < libraryAnims->getAnimation_array().getCount(); iAnim++)
         animations.append(libraryAnims->getAnimation_array()[iAnim]);
   }

   if (!animations.getCount())
      return;

   daeErrorHandler::get()->handleWarning("Creating cyclic animation clip to "
      "hold all animations");

   // Get animation_clip library (create one if necessary)
   if (!root->getLibrary_animation_clips_array().getCount()) {
      root->createAndPlace("library_animation_clips");
   }
   domLibrary_animation_clips* libraryClips = root->getLibrary_animation_clips_array()[0];

   // Create new animation_clip for the default sequence
   CREATE_ELEMENT(libraryClips, animation_clip, domAnimation_clip)
   animation_clip->setName("ambient");
   animation_clip->setId("dummy_ambient_clip");
   animation_clip->setStart(0);
   animation_clip->setEnd(0);

   // Add all top_level animations to the clip (sub-animations will be included
   // when the clip is procesed)
   for (S32 iAnim = 0; iAnim < animations.getCount(); iAnim++) {
      if (!animations[iAnim]->getId())
         animations[iAnim]->setId(avar("dummy-animation-id%d", iAnim));
      CREATE_ELEMENT(animation_clip, instance_animation, domInstanceWithExtra)
      std::string url(std::string("#") + animations[iAnim]->getId());
      instance_animation->setUrl(url.c_str());
   }

   // Add the 'Torque' profile to specify the 'Cyclic' flag
   CREATE_ELEMENT(animation_clip, extra, domExtra)
   CREATE_ELEMENT(extra, technique, domTechnique)
   CREATE_ELEMENT(technique, any, domAny)
   technique->setProfile("Torque");
   any->setElementName("cyclic");
   any->setValue("1");
}

static void conditioner_fixupAnimation(domAnimation* anim)
{
   S32 visibilityLen = dStrlen("/visibility");

   for (S32 iChannel = 0; iChannel < anim->getChannel_array().getCount(); iChannel++) {

      // Get the animation elements: <channel>, <sampler>
      domChannel* channel = anim->getChannel_array()[iChannel];
      domSampler* sampler = daeSafeCast<domSampler>(channel->getSource().getElement());
      if (!sampler)
         continue;
/*
      // If using a spline interpolation type but no tangents are specified,
      // fall back to LINEAR interpolation.
      bool isSpline = false;
      bool foundInTangent = false;
      bool foundOutTangent = false;
      for (int iInput = 0; iInput < sampler->getInput_array().getCount(); iInput++) {
         const char *semantic = sampler->getInput_array()[iInput]->getSemantic();
         if (dStrEqual(semantic, "INTERPOLATION")) {
            if (
         }
         if (dStrEqual(semantic, "IN_TANGENT"))
            foundInTangent = true;
         if (dStrEqual(semantic, "OUT_TANGENT"))
            foundOutTangent = true;
      }

      if (isSpline && (!foundInTangent || !foundOutTangent)) {
         daeErrorHandler::get()->handleWarning(avar("%s type interpolation "
            "specified for %s, but IN/OUT TANGENTS are not provided. Using "
            "LINEAR interpolation instead.");

      }
*/

      // Find the animation channel target
      daeSIDResolver resolver(channel, channel->getTarget());
      daeElement* target = resolver.getElement();
      if (!target) {

         // Some exporters generate visibility animations but don't add the
         // FCOLLADA extension, so the target doesn't actually exist! Detect
         // this situation and add the extension manually so the animation
         // still works.
         if (dStrEndsWith(channel->getTarget(), "/visibility")) {

            // Get parent SID string
            char *parentSID = dStrdup(channel->getTarget());
            parentSID[dStrlen(parentSID) - visibilityLen] = '\0';

            // Find the parent element (should be a <node>)
            daeSIDResolver parentResolver(channel, parentSID);
            daeElement* parent = parentResolver.getElement();
            delete [] parentSID;

            if (parent && (parent->getElementType() == COLLADA_TYPE::NODE)) {

               // Create the FCOLLADA extension
               daeErrorHandler::get()->handleWarning(avar("Creating missing "
                  "visibility animation target: %s", channel->getTarget()));

               // Check if the <visibility> element exists but is missing the SID
               daeElement* vis = parent->getDescendant("visibility");
               if (vis)
               {
                  vis->setAttribute("sid", "visibility");
               }
               else
               {
                  CREATE_ELEMENT(parent, extra, domExtra)
                  CREATE_ELEMENT(extra, technique, domTechnique)
                  CREATE_ELEMENT(technique, any, domAny)

                  technique->setProfile("FCOLLADA");
                  any->setElementName("visibility");
                  any->setAttribute("sid", "visibility");
                  any->setValue("");  // real initial value will be set when animation is processed in ColladaShapeLoader::processAnimation
               }
            }
         }
      }
   }

   // Process child animations
   for (S32 iAnim = 0; iAnim < anim->getAnimation_array().getCount(); iAnim++)
      conditioner_fixupAnimation(anim->getAnimation_array()[iAnim]);
}

/// Apply the set of model conditioners
void ColladaUtils::applyConditioners(domCOLLADA* root)
{
   //--------------------------------------------------------------------------
   // The built-in MAX FBX exporter specifies an <image> SID in the <texture>
   // "texture" attribute instead of a <newparam> SID. Detect and fix this.
   conditioner_fixupTextureSIDs(root);

   //--------------------------------------------------------------------------
   // The built-in MAX FBX exporter also generates invalid URI paths in the
   // <image>.<init_from> tag, so fix that up too.
   conditioner_fixupImageURIs(root);

   //--------------------------------------------------------------------------
   // Many exporters get transparency backwards. Check if the model was exported
   // by one with a known issue and correct it.
   conditioner_fixupTransparency(root);

   //--------------------------------------------------------------------------
   // Some exporters (AutoDesk) generate invalid bind_shape matrices. Warn if
   // the bind_shape_matrix is not invertible.
   conditioner_checkBindShapeMatrix(root);

   //--------------------------------------------------------------------------
   // The PoserPro exporter points the <vertex_weights> JOINT input to the
   // inverse bind matrices instead of the joint names array. Detect and fix it.
   conditioner_fixupVertexWeightJoints(root);

   //--------------------------------------------------------------------------
   // If the model contains <animation>s but no <animation_clip>s, just put all
   // top level animations into a single clip.
   conditioner_createDefaultClip(root);

   //--------------------------------------------------------------------------
   // Apply some animation fixups:
   // 1) Some exporters (eg. Blender) generate "BEZIER" type animation curves,
   //    but do not specify the IN and OUT tangent data arrays. Detect this and
   //    fall back to LINEAR interpolation.
   // 2) Some exporters generate visibility animations but don't add the FCOLLADA
   //    extension, so the target doesn't actually exist! Detect this situation
   //    and add the extension manually so the animation still works.
   for (S32 iLib = 0; iLib < root->getLibrary_animations_array().getCount(); iLib++) {
      const domLibrary_animations* lib = root->getLibrary_animations_array()[iLib];
      for (S32 iAnim = 0; iAnim < lib->getAnimation_array().getCount(); iAnim++)
         conditioner_fixupAnimation(lib->getAnimation_array()[iAnim]);
   }
}

Torque::Path ColladaUtils::findTexture(const Torque::Path& diffuseMap)
{
   Vector<Torque::Path> foundPaths;

   GBitmap::sFindFiles(diffuseMap, &foundPaths);

   if (foundPaths.size() > 0)
      return Torque::Path(foundPaths[0]);

   // If unable to load texture in current directory
   // look in the parent directory.  But never look in the root.
   Torque::Path newPath(diffuseMap);

   String filePath = newPath.getPath();

   String::SizeType  slash = filePath.find('/', filePath.length(), String::Right);

   if (slash != String::NPos)
   {
      slash = filePath.find('/', filePath.length(), String::Right);

      if (slash != String::NPos)
      {
         String truncPath = filePath.substr(0, slash);
         newPath.setPath(truncPath);

         return findTexture(newPath);
      }
   }

   return String::EmptyString;
}

void ColladaUtils::exportColladaHeader(tinyxml2::XMLElement* rootNode)
{
   tinyxml2::XMLDocument* doc = rootNode->GetDocument();
   tinyxml2::XMLElement* assetNode = doc->NewElement("asset");
   rootNode->LinkEndChild(assetNode);

   tinyxml2::XMLElement* contributorNode = doc->NewElement("contributor");
   assetNode->LinkEndChild(contributorNode);

   tinyxml2::XMLElement* authorNode = doc->NewElement("author");
   contributorNode->LinkEndChild(authorNode);
   tinyxml2::XMLText* authorNodeText = doc->NewText("Torque3D MIT User");
   authorNode->LinkEndChild(authorNodeText);

   tinyxml2::XMLElement* authoringToolNode = doc->NewElement("authoring_tool");
   contributorNode->LinkEndChild(authoringToolNode);
   tinyxml2::XMLText* authorText = doc->NewText(avar("%s %s Object Exporter", getEngineProductString(), getVersionString()));
   authoringToolNode->LinkEndChild(authorText);

   //tinyxml2::XMLElement* commentsNode = doc->NewElement("comments");
   //contributorNode->LinkEndChild(commentsNode);

   // Get the current time
   Platform::LocalTime lt;
   Platform::getLocalTime(lt);
   String localTime = Platform::localTimeToString(lt);

   localTime.replace('\t', ' ');

   tinyxml2::XMLElement* createdNode = doc->NewElement("created");
   assetNode->LinkEndChild(createdNode);
   tinyxml2::XMLText* createdText = doc->NewText(avar("%s", localTime.c_str()));
   createdNode->LinkEndChild(createdText);

   tinyxml2::XMLElement* modifiedNode = doc->NewElement("modified");
   assetNode->LinkEndChild(modifiedNode);
   tinyxml2::XMLText* modifiedText = doc->NewText(avar("%s", localTime.c_str()));
   modifiedNode->LinkEndChild(modifiedText);

   //tinyxml2::XMLElement* revisionNode = doc->NewElement("revision");
   //assetNode->LinkEndChild(revisionNode);

   //tinyxml2::XMLElement* titleNode = doc->NewElement("title");
   //assetNode->LinkEndChild(titleNode);

   //tinyxml2::XMLElement* subjectNode = doc->NewElement("subject");
   //assetNode->LinkEndChild(subjectNode);

   //tinyxml2::XMLElement* keywordsNode = doc->NewElement("keywords");
   //assetNode->LinkEndChild(keywordsNode);

   // Torque uses Z_UP with 1 unit equal to 1 meter by default
   tinyxml2::XMLElement* unitNode = doc->NewElement("unit");
   assetNode->LinkEndChild(unitNode);
   unitNode->SetAttribute("name", "meter");
   unitNode->SetAttribute("meter", "1");

   tinyxml2::XMLElement* axisNode = doc->NewElement("up_axis");
   assetNode->LinkEndChild(axisNode);
   tinyxml2::XMLText* axisText = doc->NewText("Z_UP");
   axisNode->LinkEndChild(axisText);
}

void ColladaUtils::exportColladaMaterials(tinyxml2::XMLElement* rootNode, const OptimizedPolyList& mesh, Vector<String>& matNames, const Torque::Path& colladaFile)
{
   tinyxml2::XMLDocument* doc = rootNode->GetDocument();

   // First the image library
   tinyxml2::XMLElement* imgLibNode = doc->NewElement("library_images");
   rootNode->LinkEndChild(imgLibNode);

   for (U32 i = 0; i < mesh.mMaterialList.size(); i++)
   {
      BaseMatInstance* baseInst = mesh.mMaterialList[i];

      matNames.push_back(String::ToString("Material%d", i));

      Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
      if (!mat)
         continue;

      String diffuseMap;

	  if (mat->getName() && mat->getName()[0])
		  matNames.last() = mat->mMapTo;

      // Handle an auto-generated "Default Material" specially
      if (mat->isAutoGenerated())
      {
         Torque::Path diffusePath;

         if (mat->mDiffuseMapName[0] != StringTable->EmptyString())
            diffusePath = Torque::Path(mat->mDiffuseMapName[0]);
         else
            diffusePath = String("warningMat");

         matNames.last() = diffusePath.getFileName();
         diffuseMap += diffusePath.getFullFileName();
      }
      else
      {
         if (mat->mDiffuseMapName[0] != StringTable->EmptyString())
            diffuseMap += Torque::Path(mat->mDiffuseMapName[0]);
         else
            diffuseMap += "warningMat";
      }

      Torque::Path diffusePath = findTexture(colladaFile.getPath() + "/" + diffuseMap);

      // If we didn't get a path
      if (diffusePath.getFullPath().isNotEmpty())
         diffuseMap = Torque::Path::MakeRelativePath(diffusePath, colladaFile);

      tinyxml2::XMLElement* imageNode = doc->NewElement("image");
      imgLibNode->LinkEndChild(imageNode);
      imageNode->SetAttribute("id", avar("%s", matNames.last().c_str()));
      imageNode->SetAttribute("name", avar("%s", matNames.last().c_str()));

      tinyxml2::XMLElement* initNode = doc->NewElement("init_from");
      imageNode->LinkEndChild(initNode);
	  tinyxml2::XMLText* initText = doc->NewText(avar("file://%s", diffuseMap.c_str())); // "the file://" is needed to load the texture in some old apps (ex: 3ds Max 2009)
      initNode->LinkEndChild(initText);

   }

   // Next the effects library
   tinyxml2::XMLElement* effectLibNode = doc->NewElement("library_effects");
   rootNode->LinkEndChild(effectLibNode);

   for (U32 i = 0; i < mesh.mMaterialList.size(); i++)
   {
      BaseMatInstance* baseInst = mesh.mMaterialList[i];

      Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
      if (!mat)
         continue;

      tinyxml2::XMLElement* effectNode = doc->NewElement("effect");
      effectLibNode->LinkEndChild(effectNode);
      effectNode->SetAttribute("id", avar("%s-effect", matNames[i].c_str()));
      effectNode->SetAttribute("name", avar("%s-effect", matNames[i].c_str()));

      tinyxml2::XMLElement* profileNode = doc->NewElement("profile_COMMON");
      effectNode->LinkEndChild(profileNode);
	  
	  // ---------------------------
      tinyxml2::XMLElement* newParamNode = doc->NewElement("newparam");
      profileNode->LinkEndChild(newParamNode);
      newParamNode->SetAttribute("sid", avar("%s-surface", matNames[i].c_str()));	  

      tinyxml2::XMLElement* surfaceNode = doc->NewElement("surface");
      newParamNode->LinkEndChild(surfaceNode);
	  surfaceNode->SetAttribute("type", "2D");

      tinyxml2::XMLElement* initNode2 = doc->NewElement("init_from");
      surfaceNode->LinkEndChild(initNode2);
      tinyxml2::XMLText* init2Text = doc->NewText(avar("%s", matNames[i].c_str()));
      initNode2->LinkEndChild(init2Text);

      tinyxml2::XMLElement* formatNode = doc->NewElement("format");
      surfaceNode->LinkEndChild(formatNode);
      tinyxml2::XMLText* formatText = doc->NewText("A8R8G8B8");
      formatNode->LinkEndChild(formatText);

	  // ---------------------------
      tinyxml2::XMLElement* newParam2Node = doc->NewElement("newparam");
      profileNode->LinkEndChild(newParam2Node);
      newParam2Node->SetAttribute("sid", avar("%s-sampler", matNames[i].c_str()));	  

      tinyxml2::XMLElement* sampler2DNode = doc->NewElement("sampler2D");
      newParam2Node->LinkEndChild(sampler2DNode);
	  
      tinyxml2::XMLElement* sourceSampler2DNode = doc->NewElement("source");
      sampler2DNode->LinkEndChild(sourceSampler2DNode);  
      tinyxml2::XMLText* sourceSampler2DText = doc->NewText(avar("%s-surface", matNames[i].c_str()));
      sourceSampler2DNode->LinkEndChild(sourceSampler2DText);

	  // ---------------------------

      tinyxml2::XMLElement* techniqueNode = doc->NewElement("technique");
      profileNode->LinkEndChild(techniqueNode);
      techniqueNode->SetAttribute("sid", "common");

      tinyxml2::XMLElement* blinnNode = doc->NewElement("blinn");
      techniqueNode->LinkEndChild(blinnNode);
	  
	  // ---------------------------
	  tinyxml2::XMLElement* emissionNode = doc->NewElement("emission");
	  blinnNode->LinkEndChild(emissionNode);

      tinyxml2::XMLElement* colorEmissionNode = doc->NewElement("color");
      emissionNode->LinkEndChild(colorEmissionNode);
	  colorEmissionNode->SetAttribute("sid", "emission"); 

	  tinyxml2::XMLText* colorEmissionNodeText = doc->NewText("0.0 0.0 0.0 1.0");
	  colorEmissionNode->LinkEndChild(colorEmissionNodeText);
	  
	  // ---------------------------
	  tinyxml2::XMLElement* ambientNode = doc->NewElement("ambient");
	  blinnNode->LinkEndChild(ambientNode);

      tinyxml2::XMLElement* colorAmbientNode = doc->NewElement("color");
      ambientNode->LinkEndChild(colorAmbientNode);
	  colorAmbientNode->SetAttribute("sid", "ambient"); 

	  tinyxml2::XMLText* colorAmbientNodeText = doc->NewText("0.0 0.0 0.0 1.0");
	  colorAmbientNode->LinkEndChild(colorAmbientNodeText);	  
	  
	  // ---------------------------
	  tinyxml2::XMLElement* diffuseNode = doc->NewElement("diffuse");
      blinnNode->LinkEndChild(diffuseNode);
	  tinyxml2::XMLElement* textureDiffuseNode = doc->NewElement("texture");
      diffuseNode->LinkEndChild(textureDiffuseNode);
	  textureDiffuseNode->SetAttribute("texture", avar("%s-sampler", matNames[i].c_str()));
	  textureDiffuseNode->SetAttribute("texcoord", "UVMap");
	  
      // Extra info useful for getting the texture to show up correctly in MAYA and 3DS Max
      tinyxml2::XMLElement* extraNode = doc->NewElement("extra");
      textureDiffuseNode->LinkEndChild(extraNode);

      tinyxml2::XMLElement* extraTechNode = doc->NewElement("technique");
      extraNode->LinkEndChild(extraTechNode);
      extraTechNode->SetAttribute("profile", "MAYA");

      tinyxml2::XMLElement* extraWrapUNode = doc->NewElement("wrapU");
      extraTechNode->LinkEndChild(extraWrapUNode);
      extraWrapUNode->SetAttribute("sid", "wrapU0");

      tinyxml2::XMLText* extraWrapUText = doc->NewText("TRUE");
      extraWrapUNode->LinkEndChild(extraWrapUText);

      tinyxml2::XMLElement* extraWrapVNode = doc->NewElement("wrapV");
      extraTechNode->LinkEndChild(extraWrapVNode);
      extraWrapVNode->SetAttribute("sid", "wrapV0");

      tinyxml2::XMLText* extraWrapVText = doc->NewText("TRUE");
      extraWrapVNode->LinkEndChild(extraWrapVText);

      tinyxml2::XMLElement* extraBlendNode = doc->NewElement("blend_mode");
      extraTechNode->LinkEndChild(extraBlendNode);

      tinyxml2::XMLText* extraBlendText = doc->NewText("ADD");
      extraBlendNode->LinkEndChild(extraBlendText);
	  
	  // ---------------------------
      tinyxml2::XMLElement* specularNode = doc->NewElement("specular");
      blinnNode->LinkEndChild(specularNode);

      tinyxml2::XMLElement* colorSpecularNode = doc->NewElement("color");
      specularNode->LinkEndChild(colorSpecularNode);
	  colorSpecularNode->SetAttribute("sid", "specular");
		  
	  tinyxml2::XMLText* colorSpecularNodeText = doc->NewText("0.5 0.5 0.5 1.0");
	  colorSpecularNode->LinkEndChild(colorSpecularNodeText);

	  // ---------------------------	
      tinyxml2::XMLElement* shininessNode = doc->NewElement("shininess");
      blinnNode->LinkEndChild(shininessNode);

      tinyxml2::XMLElement* colorShininessNode = doc->NewElement("float");
      shininessNode->LinkEndChild(colorShininessNode);
	  colorShininessNode->SetAttribute("sid", "shininess");
		  
	  tinyxml2::XMLText* colorShininessNodeText = doc->NewText("1.0");
	  colorShininessNode->LinkEndChild(colorShininessNodeText);

	  // ---------------------------	
      tinyxml2::XMLElement* reflectiveNode = doc->NewElement("reflective");
      blinnNode->LinkEndChild(reflectiveNode);

      tinyxml2::XMLElement* colorReflectiveNode = doc->NewElement("color");
      reflectiveNode->LinkEndChild(colorReflectiveNode);
	  colorReflectiveNode->SetAttribute("sid", "reflective");
		  
	  tinyxml2::XMLText* colorReflectiveNodeText = doc->NewText("0.0 0.0 0.0 1.0");
	  colorReflectiveNode->LinkEndChild(colorReflectiveNodeText);	  

	  // ---------------------------	
      tinyxml2::XMLElement* reflectivityNode = doc->NewElement("reflectivity");
      blinnNode->LinkEndChild(reflectivityNode);

      tinyxml2::XMLElement* floatReflectivityNode = doc->NewElement("float");
      reflectivityNode->LinkEndChild(floatReflectivityNode);
	  floatReflectivityNode->SetAttribute("sid", "reflectivity");
		  
	  tinyxml2::XMLText* floatReflectivityText = doc->NewText("0.5");
	  floatReflectivityNode->LinkEndChild(floatReflectivityText);	
	  
	  // ---------------------------	
      tinyxml2::XMLElement* transparentNode = doc->NewElement("transparent");
      blinnNode->LinkEndChild(transparentNode);
	  transparentNode->SetAttribute("opaque", "RGB_ZERO");

      tinyxml2::XMLElement* colorTransparentNode = doc->NewElement("color");
      transparentNode->LinkEndChild(colorTransparentNode);
	  colorTransparentNode->SetAttribute("sid", "transparent");
		  
	  tinyxml2::XMLText* colorTransparentNodeText = doc->NewText("0.0 0.0 0.0 1.0");
	  colorTransparentNode->LinkEndChild(colorTransparentNodeText);	

	  // ---------------------------	
      tinyxml2::XMLElement* transparencyNode = doc->NewElement("transparency");
      blinnNode->LinkEndChild(transparencyNode);

      tinyxml2::XMLElement* floatTransparencyNode = doc->NewElement("float");
      transparencyNode->LinkEndChild(floatTransparencyNode);
	  floatTransparencyNode->SetAttribute("sid", "transparency");
		  
	  tinyxml2::XMLText* floatTransparencyText = doc->NewText("0.0");
	  floatTransparencyNode->LinkEndChild(floatTransparencyText);	 
	  
	  // ---------------------------	  
      tinyxml2::XMLElement* refractionNode = doc->NewElement("index_of_refraction");
      blinnNode->LinkEndChild(refractionNode);

      tinyxml2::XMLElement* colorRefractionNode = doc->NewElement("float");
      refractionNode->LinkEndChild(colorRefractionNode);
	  colorRefractionNode->SetAttribute("sid", "index_of_refraction");
		  
	  tinyxml2::XMLText* colorRefractionNodeText = doc->NewText("1");
	  colorRefractionNode->LinkEndChild(colorRefractionNodeText); 
   }

   // Finally the material library
   tinyxml2::XMLElement* matLibNode = doc->NewElement("library_materials");
   rootNode->LinkEndChild(matLibNode);

   for (U32 i = 0; i < mesh.mMaterialList.size(); i++)
   {
      BaseMatInstance* baseInst = mesh.mMaterialList[i];

      Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
      if (!mat)
         continue;

      tinyxml2::XMLElement* materialNode = doc->NewElement("material");
      matLibNode->LinkEndChild(materialNode);
      materialNode->SetAttribute("id", avar("%s-material", matNames[i].c_str()));
      materialNode->SetAttribute("name", matNames[i].c_str());

      tinyxml2::XMLElement* instEffectNode = doc->NewElement("instance_effect");
      materialNode->LinkEndChild(instEffectNode);
      instEffectNode->SetAttribute("url", avar("#%s-effect", matNames[i].c_str()));
   }   
   
}

void ColladaUtils::exportColladaMaterials(tinyxml2::XMLElement* rootNode, const ExportData& exportData, const Torque::Path& colladaFile)
{
   tinyxml2::XMLDocument* doc = rootNode->GetDocument();

   // First the image library
   tinyxml2::XMLElement* imgLibNode = doc->NewElement("library_images");
   rootNode->LinkEndChild(imgLibNode);

   Vector<String> matNames;

   for (U32 i = 0; i < exportData.materials.size(); i++)
   {
      BaseMatInstance* baseInst = exportData.materials[i];

      matNames.push_back(String::ToString("Material%d", i));

      Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
      if (!mat)
         continue;

      String diffuseMap;

      if (mat->getName() && mat->getName()[0])
         matNames.last() = mat->mMapTo;

      // Handle an auto-generated "Default Material" specially
      if (mat->isAutoGenerated())
      {
         Torque::Path diffusePath;

         if (mat->mDiffuseMapName[0] != StringTable->EmptyString())
            diffusePath = Torque::Path(mat->mDiffuseMapName[0]);
         else
            diffusePath = String("warningMat");

         matNames.last() = diffusePath.getFileName();
         diffuseMap += diffusePath.getFullFileName();
      }
      else
      {
         if (mat->mDiffuseMapName[0] != StringTable->EmptyString())
            diffuseMap += Torque::Path(mat->mDiffuseMapName[0]);
         else
            diffuseMap += "warningMat";
      }

      Torque::Path diffusePath = findTexture(colladaFile.getPath() + "/" + diffuseMap);

      // If we didn't get a path
      if (diffusePath.getFullPath().isNotEmpty())
         diffuseMap = Torque::Path::MakeRelativePath(diffusePath, colladaFile);

      tinyxml2::XMLElement* imageNode = doc->NewElement("image");
      imgLibNode->LinkEndChild(imageNode);
      imageNode->SetAttribute("id", avar("%s", matNames.last().c_str()));
      imageNode->SetAttribute("name", avar("%s", matNames.last().c_str()));

      tinyxml2::XMLElement* initNode = doc->NewElement("init_from");
      imageNode->LinkEndChild(initNode);
      tinyxml2::XMLText* initText = doc->NewText(avar("file://%s", diffuseMap.c_str())); // "the file://" is needed to load the texture in some old apps (ex: 3ds Max 2009)
      initNode->LinkEndChild(initText);
   }

   // Next the effects library
   tinyxml2::XMLElement* effectLibNode = doc->NewElement("library_effects");
   rootNode->LinkEndChild(effectLibNode);

   for (U32 i = 0; i < exportData.materials.size(); i++)
   {
      BaseMatInstance* baseInst = exportData.materials[i];

      Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
      if (!mat)
         continue;

      tinyxml2::XMLElement* effectNode = doc->NewElement("effect");
      effectLibNode->LinkEndChild(effectNode);
      effectNode->SetAttribute("id", avar("%s-effect", matNames[i].c_str()));
      effectNode->SetAttribute("name", avar("%s-effect", matNames[i].c_str()));

      tinyxml2::XMLElement* profileNode = doc->NewElement("profile_COMMON");
      effectNode->LinkEndChild(profileNode);

      // ---------------------------
      tinyxml2::XMLElement* newParamNode = doc->NewElement("newparam");
      profileNode->LinkEndChild(newParamNode);
      newParamNode->SetAttribute("sid", avar("%s-surface", matNames[i].c_str()));

      tinyxml2::XMLElement* surfaceNode = doc->NewElement("surface");
      newParamNode->LinkEndChild(surfaceNode);
      surfaceNode->SetAttribute("type", "2D");

      tinyxml2::XMLElement* initNode2 = doc->NewElement("init_from");
      surfaceNode->LinkEndChild(initNode2);
      tinyxml2::XMLText* init2Text = doc->NewText(avar("%s", matNames[i].c_str()));
      initNode2->LinkEndChild(init2Text);

      tinyxml2::XMLElement* formatNode = doc->NewElement("format");
      surfaceNode->LinkEndChild(formatNode);
      tinyxml2::XMLText* formatText = doc->NewText("A8R8G8B8");
      formatNode->LinkEndChild(formatText);

      // ---------------------------
      tinyxml2::XMLElement* newParam2Node = doc->NewElement("newparam");
      profileNode->LinkEndChild(newParam2Node);
      newParam2Node->SetAttribute("sid", avar("%s-sampler", matNames[i].c_str()));

      tinyxml2::XMLElement* sampler2DNode = doc->NewElement("sampler2D");
      newParam2Node->LinkEndChild(sampler2DNode);

      tinyxml2::XMLElement* sourceSampler2DNode = doc->NewElement("source");
      sampler2DNode->LinkEndChild(sourceSampler2DNode);
      tinyxml2::XMLText* sourceSampler2DText = doc->NewText(avar("%s-surface", matNames[i].c_str()));
      sourceSampler2DNode->LinkEndChild(sourceSampler2DText);

      // ---------------------------

      tinyxml2::XMLElement* techniqueNode = doc->NewElement("technique");
      profileNode->LinkEndChild(techniqueNode);
      techniqueNode->SetAttribute("sid", "common");

      tinyxml2::XMLElement* blinnNode = doc->NewElement("blinn");
      techniqueNode->LinkEndChild(blinnNode);

      // ---------------------------
      tinyxml2::XMLElement* emissionNode = doc->NewElement("emission");
      blinnNode->LinkEndChild(emissionNode);

      tinyxml2::XMLElement* colorEmissionNode = doc->NewElement("color");
      emissionNode->LinkEndChild(colorEmissionNode);
      colorEmissionNode->SetAttribute("sid", "emission");

      tinyxml2::XMLText* colorEmissionNodeText = doc->NewText("0.0 0.0 0.0 1.0");
      colorEmissionNode->LinkEndChild(colorEmissionNodeText);

      // ---------------------------
      tinyxml2::XMLElement* ambientNode = doc->NewElement("ambient");
      blinnNode->LinkEndChild(ambientNode);

      tinyxml2::XMLElement* colorAmbientNode = doc->NewElement("color");
      ambientNode->LinkEndChild(colorAmbientNode);
      colorAmbientNode->SetAttribute("sid", "ambient");

      tinyxml2::XMLText* colorAmbientNodeText = doc->NewText("0.0 0.0 0.0 1.0");
      colorAmbientNode->LinkEndChild(colorAmbientNodeText);

      // ---------------------------
      tinyxml2::XMLElement* diffuseNode = doc->NewElement("diffuse");
      blinnNode->LinkEndChild(diffuseNode);
      tinyxml2::XMLElement* textureDiffuseNode = doc->NewElement("texture");
      diffuseNode->LinkEndChild(textureDiffuseNode);
      textureDiffuseNode->SetAttribute("texture", avar("%s-sampler", matNames[i].c_str()));
      textureDiffuseNode->SetAttribute("texcoord", "UVMap");

      // Extra info useful for getting the texture to show up correctly in MAYA and 3DS Max
      tinyxml2::XMLElement* extraNode = doc->NewElement("extra");
      textureDiffuseNode->LinkEndChild(extraNode);

      tinyxml2::XMLElement* extraTechNode = doc->NewElement("technique");
      extraNode->LinkEndChild(extraTechNode);
      extraTechNode->SetAttribute("profile", "MAYA");

      tinyxml2::XMLElement* extraWrapUNode = doc->NewElement("wrapU");
      extraTechNode->LinkEndChild(extraWrapUNode);
      extraWrapUNode->SetAttribute("sid", "wrapU0");

      tinyxml2::XMLText* extraWrapUText = doc->NewText("TRUE");
      extraWrapUNode->LinkEndChild(extraWrapUText);

      tinyxml2::XMLElement* extraWrapVNode = doc->NewElement("wrapV");
      extraTechNode->LinkEndChild(extraWrapVNode);
      extraWrapVNode->SetAttribute("sid", "wrapV0");

      tinyxml2::XMLText* extraWrapVText = doc->NewText("TRUE");
      extraWrapVNode->LinkEndChild(extraWrapVText);

      tinyxml2::XMLElement* extraBlendNode = doc->NewElement("blend_mode");
      extraTechNode->LinkEndChild(extraBlendNode);

      tinyxml2::XMLText* extraBlendText = doc->NewText("ADD");
      extraBlendNode->LinkEndChild(extraBlendText);

      // ---------------------------
      tinyxml2::XMLElement* specularNode = doc->NewElement("specular");
      blinnNode->LinkEndChild(specularNode);

      tinyxml2::XMLElement* colorSpecularNode = doc->NewElement("color");
      specularNode->LinkEndChild(colorSpecularNode);
      colorSpecularNode->SetAttribute("sid", "specular");

      tinyxml2::XMLText* colorSpecularNodeText = doc->NewText("0.5 0.5 0.5 1.0");
      colorSpecularNode->LinkEndChild(colorSpecularNodeText);

      // ---------------------------	
      tinyxml2::XMLElement* shininessNode = doc->NewElement("shininess");
      blinnNode->LinkEndChild(shininessNode);

      tinyxml2::XMLElement* colorShininessNode = doc->NewElement("float");
      shininessNode->LinkEndChild(colorShininessNode);
      colorShininessNode->SetAttribute("sid", "shininess");

      tinyxml2::XMLText* colorShininessNodeText = doc->NewText("1.0");
      colorShininessNode->LinkEndChild(colorShininessNodeText);

      // ---------------------------	
      tinyxml2::XMLElement* reflectiveNode = doc->NewElement("reflective");
      blinnNode->LinkEndChild(reflectiveNode);

      tinyxml2::XMLElement* colorReflectiveNode = doc->NewElement("color");
      reflectiveNode->LinkEndChild(colorReflectiveNode);
      colorReflectiveNode->SetAttribute("sid", "reflective");

      tinyxml2::XMLText* colorReflectiveNodeText = doc->NewText("0.0 0.0 0.0 1.0");
      colorReflectiveNode->LinkEndChild(colorReflectiveNodeText);

      // ---------------------------	
      tinyxml2::XMLElement* reflectivityNode = doc->NewElement("reflectivity");
      blinnNode->LinkEndChild(reflectivityNode);

      tinyxml2::XMLElement* floatReflectivityNode = doc->NewElement("float");
      reflectivityNode->LinkEndChild(floatReflectivityNode);
      floatReflectivityNode->SetAttribute("sid", "reflectivity");

      tinyxml2::XMLText* floatReflectivityText = doc->NewText("0.5");
      floatReflectivityNode->LinkEndChild(floatReflectivityText);

      // ---------------------------	
      tinyxml2::XMLElement* transparentNode = doc->NewElement("transparent");
      blinnNode->LinkEndChild(transparentNode);
      transparentNode->SetAttribute("opaque", "RGB_ZERO");

      tinyxml2::XMLElement* colorTransparentNode = doc->NewElement("color");
      transparentNode->LinkEndChild(colorTransparentNode);
      colorTransparentNode->SetAttribute("sid", "transparent");

      tinyxml2::XMLText* colorTransparentNodeText = doc->NewText("0.0 0.0 0.0 1.0");
      colorTransparentNode->LinkEndChild(colorTransparentNodeText);

      // ---------------------------	
      tinyxml2::XMLElement* transparencyNode = doc->NewElement("transparency");
      blinnNode->LinkEndChild(transparencyNode);

      tinyxml2::XMLElement* floatTransparencyNode = doc->NewElement("float");
      transparencyNode->LinkEndChild(floatTransparencyNode);
      floatTransparencyNode->SetAttribute("sid", "transparency");

      tinyxml2::XMLText* floatTransparencyText = doc->NewText("0.0");
      floatTransparencyNode->LinkEndChild(floatTransparencyText);

      // ---------------------------	  
      tinyxml2::XMLElement* refractionNode = doc->NewElement("index_of_refraction");
      blinnNode->LinkEndChild(refractionNode);

      tinyxml2::XMLElement* colorRefractionNode = doc->NewElement("float");
      refractionNode->LinkEndChild(colorRefractionNode);
      colorRefractionNode->SetAttribute("sid", "index_of_refraction");

      tinyxml2::XMLText* colorRefractionNodeText = doc->NewText("1");
      colorRefractionNode->LinkEndChild(colorRefractionNodeText);
   }

   // Finally the material library
   tinyxml2::XMLElement* matLibNode = doc->NewElement("library_materials");
   rootNode->LinkEndChild(matLibNode);

   for (U32 i = 0; i < exportData.materials.size(); i++)
   {
      BaseMatInstance* baseInst = exportData.materials[i];

      Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
      if (!mat)
         continue;

      tinyxml2::XMLElement* materialNode = doc->NewElement("material");
      matLibNode->LinkEndChild(materialNode);
      materialNode->SetAttribute("id", avar("%s-material", matNames[i].c_str()));
      materialNode->SetAttribute("name", matNames[i].c_str());

      tinyxml2::XMLElement* instEffectNode = doc->NewElement("instance_effect");
      materialNode->LinkEndChild(instEffectNode);
      instEffectNode->SetAttribute("url", avar("#%s-effect", matNames[i].c_str()));
   }

}

void ColladaUtils::exportColladaTriangles(tinyxml2::XMLElement* meshNode, const OptimizedPolyList& mesh, const String& meshName, const Vector<String>& matNames)
{
   tinyxml2::XMLDocument* doc = meshNode->GetDocument();

   // Start at -1 so we will export polygons that do not have a material.
   for (S32 i = -1; i < matNames.size(); i++)
   {
      // Calculate the number of triangles that uses this Material
      U32 triangleCount = 0;

      for (U32 j = 0; j < mesh.mPolyList.size(); j++)
      {
         const OptimizedPolyList::Poly& poly = mesh.mPolyList[j];

         if (poly.material != i)
            continue;

         if (poly.vertexCount < 3)
            continue;

         if (poly.type == OptimizedPolyList::TriangleList ||
             poly.type == OptimizedPolyList::TriangleFan ||
             poly.type == OptimizedPolyList::TriangleStrip)
         {
            triangleCount += poly.vertexCount - 2;
         }
         else
            AssertISV(false, "ColladaUtils::exportColladaTriangles(): Unknown Poly type!");
      }

      // Make sure that we are actually using this Material
      if (triangleCount == 0)
         continue;

      tinyxml2::XMLElement* trianglesNode = doc->NewElement("triangles");
      meshNode->LinkEndChild(trianglesNode);
      trianglesNode->SetAttribute("material", ( i > -1 ) ? avar("%s-material", matNames[i].c_str()) : "" );
      trianglesNode->SetAttribute("count", avar("%d", triangleCount));

      tinyxml2::XMLElement* trianglesVertInputNode = doc->NewElement("input");
      trianglesNode->LinkEndChild(trianglesVertInputNode);
      trianglesVertInputNode->SetAttribute("semantic", "VERTEX");
      trianglesVertInputNode->SetAttribute("source", avar("#%s-mesh-vertices", meshName.c_str()));
      trianglesVertInputNode->SetAttribute("offset", "0");

      tinyxml2::XMLElement* trianglesNormalInputNode = doc->NewElement("input");
      trianglesNode->LinkEndChild(trianglesNormalInputNode);
      trianglesNormalInputNode->SetAttribute("semantic", "NORMAL");
      trianglesNormalInputNode->SetAttribute("source", avar("#%s-mesh-normals", meshName.c_str()));
      trianglesNormalInputNode->SetAttribute("offset", "1");

      tinyxml2::XMLElement* trianglesUV0InputNode = doc->NewElement("input");
      trianglesNode->LinkEndChild(trianglesUV0InputNode);
      trianglesUV0InputNode->SetAttribute("semantic", "TEXCOORD");
      trianglesUV0InputNode->SetAttribute("source", avar("#%s-mesh-map-0", meshName.c_str()));
      trianglesUV0InputNode->SetAttribute("offset", "2");
      trianglesUV0InputNode->SetAttribute("set", "0");

      tinyxml2::XMLElement* polyNode = doc->NewElement("p");
      trianglesNode->LinkEndChild(polyNode);

      Vector<U32> tempIndices;
      tempIndices.reserve(4);

      for (U32 j = 0; j < mesh.mPolyList.size(); j++)
      {
         const OptimizedPolyList::Poly& poly = mesh.mPolyList[j];

         if (poly.vertexCount < 3)
            continue;

         if (poly.material != i)
            continue;

         tempIndices.setSize(poly.vertexCount);
         dMemset(tempIndices.address(), 0, poly.vertexCount);

         if (poly.type == OptimizedPolyList::TriangleStrip)
         {
            tempIndices[0] = 0;
            U32 idx = 1;
            
            for (U32 k = 1; k < poly.vertexCount; k += 2)
               tempIndices[idx++] = k;

            for (U32 k = ((poly.vertexCount - 1) & (~0x1)); k > 0; k -= 2)
               tempIndices[idx++] = k;
         }
         else if (poly.type == OptimizedPolyList::TriangleList ||
                  poly.type == OptimizedPolyList::TriangleFan)
         {
            for (U32 k = 0; k < poly.vertexCount; k++)
               tempIndices[k] = k;
         }
         else
            AssertISV(false, "ColladaUtils::exportColladaTriangles(): Unknown Poly type!");

         const U32& firstIdx = mesh.mIndexList[poly.vertexStart];
         const OptimizedPolyList::VertIndex& firstVertIdx = mesh.mVertexList[firstIdx];

         for (U32 k = 1; k < poly.vertexCount - 1; k++)
         {
            const U32& secondIdx = mesh.mIndexList[poly.vertexStart + tempIndices[k]];
            const U32& thirdIdx  = mesh.mIndexList[poly.vertexStart + tempIndices[k + 1]];

            const OptimizedPolyList::VertIndex& secondVertIdx = mesh.mVertexList[secondIdx];
            const OptimizedPolyList::VertIndex& thirdVertIdx = mesh.mVertexList[thirdIdx];

            // Note the reversed winding on the triangles
            const char* tri = avar("%d %d %d %d %d %d %d %d %d ",
                                   thirdVertIdx.vertIdx, thirdVertIdx.normalIdx, thirdVertIdx.uv0Idx,
                                   secondVertIdx.vertIdx, secondVertIdx.normalIdx, secondVertIdx.uv0Idx,
                                   firstVertIdx.vertIdx, firstVertIdx.normalIdx, firstVertIdx.uv0Idx);

            tinyxml2::XMLText* triangleText = doc->NewText(tri);
            polyNode->LinkEndChild(triangleText);
         }
      }
   }
}

void ColladaUtils::exportColladaTriangles(tinyxml2::XMLElement* meshNode, const ExportData& exportData, const U32 detailLevel, const String& meshName)
{
   tinyxml2::XMLDocument* doc = meshNode->GetDocument();

   // Calculate the number of triangles that uses this Material
   U32 triangleCount = 0;

   const ExportData::detailLevel* dl = &exportData.detailLevels[detailLevel];

   for (S32 i = 0; i < dl->materialRefList.size(); i++)
   {
      int matIdx;
      dl->materialRefList.tryGetValue(i, matIdx);
      BaseMatInstance* baseInst = exportData.materials[matIdx];

      Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
      if (!mat)
         continue;

      String matName;

      if (mat->getName() && mat->getName()[0])
         matName = mat->mMapTo;

      for (U32 j = 0; j < dl->mesh.mPolyList.size(); j++)
      {
         const OptimizedPolyList::Poly& poly = dl->mesh.mPolyList[j];

         if (poly.material != i)
            continue;

         if (poly.vertexCount < 3)
            continue;

         if (poly.type == OptimizedPolyList::TriangleList ||
            poly.type == OptimizedPolyList::TriangleFan ||
            poly.type == OptimizedPolyList::TriangleStrip)
         {
            triangleCount += poly.vertexCount - 2;
         }
         else
            AssertISV(false, "ColladaUtils::exportColladaTriangles(): Unknown Poly type!");
      }

      // Make sure that we are actually using this Material
      if (triangleCount == 0)
         continue;

      tinyxml2::XMLElement* trianglesNode = doc->NewElement("triangles");
      meshNode->LinkEndChild(trianglesNode);
      trianglesNode->SetAttribute("material", (i > -1) ? avar("%s-material", matName.c_str()) : "");
      trianglesNode->SetAttribute("count", avar("%d", triangleCount));

      tinyxml2::XMLElement* trianglesVertInputNode = doc->NewElement("input");
      trianglesNode->LinkEndChild(trianglesVertInputNode);
      trianglesVertInputNode->SetAttribute("semantic", "VERTEX");
      trianglesVertInputNode->SetAttribute("source", avar("#%s-mesh-vertices", meshName.c_str()));
      trianglesVertInputNode->SetAttribute("offset", "0");

      tinyxml2::XMLElement* trianglesNormalInputNode = doc->NewElement("input");
      trianglesNode->LinkEndChild(trianglesNormalInputNode);
      trianglesNormalInputNode->SetAttribute("semantic", "NORMAL");
      trianglesNormalInputNode->SetAttribute("source", avar("#%s-mesh-normals", meshName.c_str()));
      trianglesNormalInputNode->SetAttribute("offset", "1");

      tinyxml2::XMLElement* trianglesUV0InputNode = doc->NewElement("input");
      trianglesNode->LinkEndChild(trianglesUV0InputNode);
      trianglesUV0InputNode->SetAttribute("semantic", "TEXCOORD");
      trianglesUV0InputNode->SetAttribute("source", avar("#%s-mesh-map-0", meshName.c_str()));
      trianglesUV0InputNode->SetAttribute("offset", "2");
      trianglesUV0InputNode->SetAttribute("set", "0");

      tinyxml2::XMLElement* polyNode = doc->NewElement("p");
      trianglesNode->LinkEndChild(polyNode);

      Vector<U32> tempIndices;
      tempIndices.reserve(4);

      for (U32 j = 0; j < dl->mesh.mPolyList.size(); j++)
      {
         const OptimizedPolyList::Poly& poly = dl->mesh.mPolyList[j];

         if (poly.vertexCount < 3)
            continue;

         if (poly.material != i)
            continue;

         tempIndices.setSize(poly.vertexCount);
         dMemset(tempIndices.address(), 0, poly.vertexCount);

         if (poly.type == OptimizedPolyList::TriangleStrip)
         {
            tempIndices[0] = 0;
            U32 idx = 1;

            for (U32 k = 1; k < poly.vertexCount; k += 2)
               tempIndices[idx++] = k;

            for (U32 k = ((poly.vertexCount - 1) & (~0x1)); k > 0; k -= 2)
               tempIndices[idx++] = k;
         }
         else if (poly.type == OptimizedPolyList::TriangleList ||
            poly.type == OptimizedPolyList::TriangleFan)
         {
            for (U32 k = 0; k < poly.vertexCount; k++)
               tempIndices[k] = k;
         }
         else
            AssertISV(false, "ColladaUtils::exportColladaTriangles(): Unknown Poly type!");

         const U32& firstIdx = dl->mesh.mIndexList[poly.vertexStart];
         const OptimizedPolyList::VertIndex& firstVertIdx = dl->mesh.mVertexList[firstIdx];

         for (U32 k = 1; k < poly.vertexCount - 1; k++)
         {
            const U32& secondIdx = dl->mesh.mIndexList[poly.vertexStart + tempIndices[k]];
            const U32& thirdIdx = dl->mesh.mIndexList[poly.vertexStart + tempIndices[k + 1]];

            const OptimizedPolyList::VertIndex& secondVertIdx = dl->mesh.mVertexList[secondIdx];
            const OptimizedPolyList::VertIndex& thirdVertIdx = dl->mesh.mVertexList[thirdIdx];

            // Note the reversed winding on the triangles
            const char* tri = avar("%d %d %d %d %d %d %d %d %d ",
               thirdVertIdx.vertIdx, thirdVertIdx.normalIdx, thirdVertIdx.uv0Idx,
               secondVertIdx.vertIdx, secondVertIdx.normalIdx, secondVertIdx.uv0Idx,
               firstVertIdx.vertIdx, firstVertIdx.normalIdx, firstVertIdx.uv0Idx);

            tinyxml2::XMLText* triangleText = doc->NewText(tri);
            polyNode->LinkEndChild(triangleText);
         }
      }
   }
}

void ColladaUtils::exportColladaCollisionTriangles(tinyxml2::XMLElement* meshNode, const ExportData& exportData, const U32 collisionIdx)
{
   tinyxml2::XMLDocument* doc = meshNode->GetDocument();

   // Calculate the number of triangles that uses this Material
   U32 triangleCount = 0;

   const ExportData::colMesh* col = &exportData.colMeshes[collisionIdx];

   String meshName = col->colMeshName;

   for (U32 j = 0; j < col->mesh.mPolyList.size(); j++)
   {
      const OptimizedPolyList::Poly& poly = col->mesh.mPolyList[j];

      if (poly.vertexCount < 3)
         continue;

      if (poly.type == OptimizedPolyList::TriangleList ||
         poly.type == OptimizedPolyList::TriangleFan ||
         poly.type == OptimizedPolyList::TriangleStrip)
      {
         triangleCount += poly.vertexCount - 2;
      }
      else
         AssertISV(false, "ColladaUtils::exportColladaCollisionTriangles(): Unknown Poly type!");
   }

   // Make sure that we are actually using this Material
   if (triangleCount == 0)
      return;

   tinyxml2::XMLElement* trianglesNode = doc->NewElement("triangles");
   meshNode->LinkEndChild(trianglesNode);
   trianglesNode->SetAttribute("material", "");
   trianglesNode->SetAttribute("count", avar("%d", triangleCount));

   tinyxml2::XMLElement* trianglesVertInputNode = doc->NewElement("input");
   trianglesNode->LinkEndChild(trianglesVertInputNode);
   trianglesVertInputNode->SetAttribute("semantic", "VERTEX");
   trianglesVertInputNode->SetAttribute("source", avar("#%s-mesh-vertices", meshName.c_str()));
   trianglesVertInputNode->SetAttribute("offset", "0");

   tinyxml2::XMLElement* trianglesNormalInputNode = doc->NewElement("input");
   trianglesNode->LinkEndChild(trianglesNormalInputNode);
   trianglesNormalInputNode->SetAttribute("semantic", "NORMAL");
   trianglesNormalInputNode->SetAttribute("source", avar("#%s-mesh-normals", meshName.c_str()));
   trianglesNormalInputNode->SetAttribute("offset", "1");

   tinyxml2::XMLElement* trianglesUV0InputNode = doc->NewElement("input");
   trianglesNode->LinkEndChild(trianglesUV0InputNode);
   trianglesUV0InputNode->SetAttribute("semantic", "TEXCOORD");
   trianglesUV0InputNode->SetAttribute("source", avar("#%s-mesh-map-0", meshName.c_str()));
   trianglesUV0InputNode->SetAttribute("offset", "2");
   trianglesUV0InputNode->SetAttribute("set", "0");

   tinyxml2::XMLElement* polyNode = doc->NewElement("p");
   trianglesNode->LinkEndChild(polyNode);

   Vector<U32> tempIndices;
   tempIndices.reserve(4);

   for (U32 j = 0; j < col->mesh.mPolyList.size(); j++)
   {
      const OptimizedPolyList::Poly& poly = col->mesh.mPolyList[j];

      if (poly.vertexCount < 3)
         continue;

      tempIndices.setSize(poly.vertexCount);
      dMemset(tempIndices.address(), 0, poly.vertexCount);

      if (poly.type == OptimizedPolyList::TriangleStrip)
      {
         tempIndices[0] = 0;
         U32 idx = 1;

         for (U32 k = 1; k < poly.vertexCount; k += 2)
            tempIndices[idx++] = k;

         for (U32 k = ((poly.vertexCount - 1) & (~0x1)); k > 0; k -= 2)
            tempIndices[idx++] = k;
      }
      else if (poly.type == OptimizedPolyList::TriangleList ||
         poly.type == OptimizedPolyList::TriangleFan)
      {
         for (U32 k = 0; k < poly.vertexCount; k++)
            tempIndices[k] = k;
      }
      else
         AssertISV(false, "ColladaUtils::exportColladaTriangles(): Unknown Poly type!");

      const U32& firstIdx = col->mesh.mIndexList[poly.vertexStart];
      const OptimizedPolyList::VertIndex& firstVertIdx = col->mesh.mVertexList[firstIdx];

      for (U32 k = 1; k < poly.vertexCount - 1; k++)
      {
         const U32& secondIdx = col->mesh.mIndexList[poly.vertexStart + tempIndices[k]];
         const U32& thirdIdx = col->mesh.mIndexList[poly.vertexStart + tempIndices[k + 1]];

         const OptimizedPolyList::VertIndex& secondVertIdx = col->mesh.mVertexList[secondIdx];
         const OptimizedPolyList::VertIndex& thirdVertIdx = col->mesh.mVertexList[thirdIdx];

         // Note the reversed winding on the triangles
         const char* tri = avar("%d %d %d %d %d %d %d %d %d ",
            thirdVertIdx.vertIdx, thirdVertIdx.normalIdx, thirdVertIdx.uv0Idx,
            secondVertIdx.vertIdx, secondVertIdx.normalIdx, secondVertIdx.uv0Idx,
            firstVertIdx.vertIdx, firstVertIdx.normalIdx, firstVertIdx.uv0Idx);

         tinyxml2::XMLText* triangleText = doc->NewText(tri);
         polyNode->LinkEndChild(triangleText);
      }
   }
}

void ColladaUtils::exportColladaMesh(tinyxml2::XMLElement* rootNode, const OptimizedPolyList& mesh, const String& meshName, const Vector<String>& matNames)
{
   tinyxml2::XMLDocument* doc = rootNode->GetDocument();

   tinyxml2::XMLElement* libGeomsNode = doc->NewElement("library_geometries");
   rootNode->LinkEndChild(libGeomsNode);

   tinyxml2::XMLElement* geometryNode = doc->NewElement("geometry");
   libGeomsNode->LinkEndChild(geometryNode);
   geometryNode->SetAttribute("id", avar("%s-mesh", meshName.c_str()));
   geometryNode->SetAttribute("name", avar("%s", meshName.c_str()));

   tinyxml2::XMLElement* meshNode = doc->NewElement("mesh");
   geometryNode->LinkEndChild(meshNode);

   // Save out the vertices
   tinyxml2::XMLElement* vertsSourceNode = doc->NewElement("source");
   meshNode->LinkEndChild(vertsSourceNode);
   vertsSourceNode->SetAttribute("id", avar("%s-mesh-positions", meshName.c_str()));

   tinyxml2::XMLElement* vertsNode = doc->NewElement("float_array");
   vertsSourceNode->LinkEndChild(vertsNode);
   vertsNode->SetAttribute("id", avar("%s-mesh-positions-array", meshName.c_str()));
   vertsNode->SetAttribute("count", avar("%d", mesh.mPoints.size() * 3));

   for (U32 i = 0; i < mesh.mPoints.size(); i++)
   {
      const Point3F& vert = mesh.mPoints[i];

      tinyxml2::XMLText* vertText = doc->NewText(avar("%.4f %.4f %.4f ", vert.x, vert.y, vert.z));
      vertsNode->LinkEndChild(vertText);
   }

   // Save the vertex accessor
   tinyxml2::XMLElement* vertsTechNode = doc->NewElement("technique_common");
   vertsSourceNode->LinkEndChild(vertsTechNode);

   tinyxml2::XMLElement* vertsAccNode = doc->NewElement("accessor");
   vertsTechNode->LinkEndChild(vertsAccNode);
   vertsAccNode->SetAttribute("source", avar("#%s-mesh-positions-array", meshName.c_str()));
   vertsAccNode->SetAttribute("count", avar("%d", mesh.mPoints.size()));
   vertsAccNode->SetAttribute("stride", "3");

   tinyxml2::XMLElement* vertsAccXNode = doc->NewElement("param");
   vertsAccNode->LinkEndChild(vertsAccXNode);
   vertsAccXNode->SetAttribute("name", "X");
   vertsAccXNode->SetAttribute("type", "float");

   tinyxml2::XMLElement* vertsAccYNode = doc->NewElement("param");
   vertsAccNode->LinkEndChild(vertsAccYNode);
   vertsAccYNode->SetAttribute("name", "Y");
   vertsAccYNode->SetAttribute("type", "float");

   tinyxml2::XMLElement* vertsAccZNode = doc->NewElement("param");
   vertsAccNode->LinkEndChild(vertsAccZNode);
   vertsAccZNode->SetAttribute("name", "Z");
   vertsAccZNode->SetAttribute("type", "float");

   // Save out the normals
   tinyxml2::XMLElement* normalsSourceNode = doc->NewElement("source");
   meshNode->LinkEndChild(normalsSourceNode);
   normalsSourceNode->SetAttribute("id", avar("%s-mesh-normals", meshName.c_str()));

   tinyxml2::XMLElement* normalsNode = doc->NewElement("float_array");
   normalsSourceNode->LinkEndChild(normalsNode);
   normalsNode->SetAttribute("id", avar("%s-mesh-normals-array", meshName.c_str()));
   normalsNode->SetAttribute("count", avar("%d", mesh.mNormals.size() * 3));

   for (U32 i = 0; i < mesh.mNormals.size(); i++)
   {
      const Point3F& normal = mesh.mNormals[i];

      tinyxml2::XMLText* normalText = doc->NewText(avar("%.4f %.4f %.4f ", normal.x, normal.y, normal.z));
      normalsNode->LinkEndChild(normalText);
   }

   // Save the normals accessor
   tinyxml2::XMLElement* normalsTechNode = doc->NewElement("technique_common");
   normalsSourceNode->LinkEndChild(normalsTechNode);

   tinyxml2::XMLElement* normalsAccNode = doc->NewElement("accessor");
   normalsTechNode->LinkEndChild(normalsAccNode);
   normalsAccNode->SetAttribute("source", avar("#%s-mesh-normals-array", meshName.c_str()));
   normalsAccNode->SetAttribute("count", avar("%d", mesh.mNormals.size()));
   normalsAccNode->SetAttribute("stride", "3");

   tinyxml2::XMLElement* normalsAccXNode = doc->NewElement("param");
   normalsAccNode->LinkEndChild(normalsAccXNode);
   normalsAccXNode->SetAttribute("name", "X");
   normalsAccXNode->SetAttribute("type", "float");

   tinyxml2::XMLElement* normalsAccYNode = doc->NewElement("param");
   normalsAccNode->LinkEndChild(normalsAccYNode);
   normalsAccYNode->SetAttribute("name", "Y");
   normalsAccYNode->SetAttribute("type", "float");

   tinyxml2::XMLElement* normalsAccZNode = doc->NewElement("param");
   normalsAccNode->LinkEndChild(normalsAccZNode);
   normalsAccZNode->SetAttribute("name", "Z");
   normalsAccZNode->SetAttribute("type", "float");

   // Save out the uvs
   tinyxml2::XMLElement* uv0SourceNode = doc->NewElement("source");
   meshNode->LinkEndChild(uv0SourceNode);
   uv0SourceNode->SetAttribute("id", avar("%s-mesh-map-0", meshName.c_str()));

   tinyxml2::XMLElement* uv0Node = doc->NewElement("float_array");
   uv0SourceNode->LinkEndChild(uv0Node);
   uv0Node->SetAttribute("id", avar("%s-mesh-map-0-array", meshName.c_str()));
   uv0Node->SetAttribute("count", avar("%d", mesh.mUV0s.size() * 2));

   for (U32 i = 0; i < mesh.mUV0s.size(); i++)
   {
      const Point2F& uv0 = mesh.mUV0s[i];

      tinyxml2::XMLText* uv0Text = doc->NewText(avar("%.4f %.4f ", uv0.x, 1.0f - uv0.y));   // COLLADA uvs are upside down compared to Torque
      uv0Node->LinkEndChild(uv0Text);
   }

   // Save the uv0 accessor
   tinyxml2::XMLElement* uv0TechNode = doc->NewElement("technique_common");
   uv0SourceNode->LinkEndChild(uv0TechNode);

   tinyxml2::XMLElement* uv0AccNode = doc->NewElement("accessor");
   uv0TechNode->LinkEndChild(uv0AccNode);
   uv0AccNode->SetAttribute("source", avar("#%s-mesh-map-0-array", meshName.c_str()));
   uv0AccNode->SetAttribute("count", avar("%d", mesh.mUV0s.size()));
   uv0AccNode->SetAttribute("stride", "2");

   tinyxml2::XMLElement* uv0AccSNode = doc->NewElement("param");
   uv0AccNode->LinkEndChild(uv0AccSNode);
   uv0AccSNode->SetAttribute("name", "S");
   uv0AccSNode->SetAttribute("type", "float");

   tinyxml2::XMLElement* uv0AccTNode = doc->NewElement("param");
   uv0AccNode->LinkEndChild(uv0AccTNode);
   uv0AccTNode->SetAttribute("name", "T");
   uv0AccTNode->SetAttribute("type", "float");

   // Define the vertices position array
   tinyxml2::XMLElement* verticesNode = doc->NewElement("vertices");
   meshNode->LinkEndChild(verticesNode);
   verticesNode->SetAttribute("id", avar("%s-mesh-vertices", meshName.c_str()));

   tinyxml2::XMLElement* verticesInputNode = doc->NewElement("input");
   verticesNode->LinkEndChild(verticesInputNode);
   verticesInputNode->SetAttribute("semantic", "POSITION");
   verticesInputNode->SetAttribute("source", avar("#%s-mesh-positions", meshName.c_str()));

   exportColladaTriangles(meshNode, mesh, meshName, matNames);
   
   // Extra info useful for COLLADAMaya importer (OpenCOLLADA)
   tinyxml2::XMLElement* extraGeoNode = doc->NewElement("extra");
   libGeomsNode->LinkEndChild(extraGeoNode);

   tinyxml2::XMLElement* extraGeoNodeTech = doc->NewElement("technique");
   extraGeoNode->LinkEndChild(extraGeoNodeTech);
   extraGeoNodeTech->SetAttribute("profile", "OpenCOLLADAMaya");

   tinyxml2::XMLElement* mayaNode2Id = doc->NewElement("originalMayaNodeId");
   extraGeoNodeTech->LinkEndChild(mayaNode2Id);
   mayaNode2Id->SetAttribute("sid", "originalMayaNodeId");
   tinyxml2::XMLText* mayaIdMesh = doc->NewText(avar("%s", meshName.c_str()));
   mayaNode2Id->LinkEndChild(mayaIdMesh);

   tinyxml2::XMLElement* doubleSidedId = doc->NewElement("double_sided");
   extraGeoNodeTech->LinkEndChild(doubleSidedId);
   doubleSidedId->SetAttribute("sid", "double_sided");
   tinyxml2::XMLText* doubleSideIdText = doc->NewText("1");
   doubleSidedId->LinkEndChild(doubleSideIdText);
   
   tinyxml2::XMLElement* paramExtraNode = doc->NewElement("param");
   extraGeoNodeTech->LinkEndChild(paramExtraNode);
   paramExtraNode->SetAttribute("sid", "colladaId");
   paramExtraNode->SetAttribute("type", "string");

   tinyxml2::XMLText* mayaParamMesh = doc->NewText(avar("%s-mesh", meshName.c_str()));
   paramExtraNode->LinkEndChild(mayaParamMesh);
   

}

void ColladaUtils::exportColladaMesh(tinyxml2::XMLElement* rootNode, const ExportData& exportData, const String& meshName)
{
   tinyxml2::XMLDocument* doc = rootNode->GetDocument();

   tinyxml2::XMLElement* libGeomsNode = doc->NewElement("library_geometries");
   rootNode->LinkEndChild(libGeomsNode);

   for (U32 d = 0; d < exportData.detailLevels.size(); d++)
   {
      char lodMeshName[256];
      dSprintf(lodMeshName, 256, "%s%d", meshName.c_str(), exportData.detailLevels[d].size);

      char lodMeshID[256];
      dSprintf(lodMeshID, 256, "%s-mesh", lodMeshName);

      tinyxml2::XMLElement* geometryNode = doc->NewElement("geometry");
      libGeomsNode->LinkEndChild(geometryNode);
      geometryNode->SetAttribute("id", lodMeshID);
      geometryNode->SetAttribute("name", lodMeshName);

      tinyxml2::XMLElement* meshNode = doc->NewElement("mesh");
      geometryNode->LinkEndChild(meshNode);

      // Save out the vertices
      tinyxml2::XMLElement* vertsSourceNode = doc->NewElement("source");
      meshNode->LinkEndChild(vertsSourceNode);
      vertsSourceNode->SetAttribute("id", avar("%s-mesh-positions", lodMeshName));

      tinyxml2::XMLElement* vertsNode = doc->NewElement("float_array");
      vertsSourceNode->LinkEndChild(vertsNode);
      vertsNode->SetAttribute("id", avar("%s-mesh-positions-array", lodMeshName));
      vertsNode->SetAttribute("count", avar("%d", exportData.detailLevels[d].mesh.mPoints.size() * 3));

      for (U32 i = 0; i < exportData.detailLevels[d].mesh.mPoints.size(); i++)
      {
         const Point3F& vert = exportData.detailLevels[d].mesh.mPoints[i];

         tinyxml2::XMLText* vertText = doc->NewText(avar("%.4f %.4f %.4f ", vert.x, vert.y, vert.z));
         vertsNode->LinkEndChild(vertText);
      }

      // Save the vertex accessor
      tinyxml2::XMLElement* vertsTechNode = doc->NewElement("technique_common");
      vertsSourceNode->LinkEndChild(vertsTechNode);

      tinyxml2::XMLElement* vertsAccNode = doc->NewElement("accessor");
      vertsTechNode->LinkEndChild(vertsAccNode);
      vertsAccNode->SetAttribute("source", avar("#%s-mesh-positions-array", lodMeshName));
      vertsAccNode->SetAttribute("count", avar("%d", exportData.detailLevels[d].mesh.mPoints.size()));
      vertsAccNode->SetAttribute("stride", "3");

      tinyxml2::XMLElement* vertsAccXNode = doc->NewElement("param");
      vertsAccNode->LinkEndChild(vertsAccXNode);
      vertsAccXNode->SetAttribute("name", "X");
      vertsAccXNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* vertsAccYNode = doc->NewElement("param");
      vertsAccNode->LinkEndChild(vertsAccYNode);
      vertsAccYNode->SetAttribute("name", "Y");
      vertsAccYNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* vertsAccZNode = doc->NewElement("param");
      vertsAccNode->LinkEndChild(vertsAccZNode);
      vertsAccZNode->SetAttribute("name", "Z");
      vertsAccZNode->SetAttribute("type", "float");

      // Save out the normals
      tinyxml2::XMLElement* normalsSourceNode = doc->NewElement("source");
      meshNode->LinkEndChild(normalsSourceNode);
      normalsSourceNode->SetAttribute("id", avar("%s-mesh-normals", lodMeshName));

      tinyxml2::XMLElement* normalsNode = doc->NewElement("float_array");
      normalsSourceNode->LinkEndChild(normalsNode);
      normalsNode->SetAttribute("id", avar("%s-mesh-normals-array", lodMeshName));
      normalsNode->SetAttribute("count", avar("%d", exportData.detailLevels[d].mesh.mNormals.size() * 3));

      for (U32 i = 0; i < exportData.detailLevels[d].mesh.mNormals.size(); i++)
      {
         const Point3F& normal = exportData.detailLevels[d].mesh.mNormals[i];

         tinyxml2::XMLText* normalText = doc->NewText(avar("%.4f %.4f %.4f ", normal.x, normal.y, normal.z));
         normalsNode->LinkEndChild(normalText);
      }

      // Save the normals accessor
      tinyxml2::XMLElement* normalsTechNode = doc->NewElement("technique_common");
      normalsSourceNode->LinkEndChild(normalsTechNode);

      tinyxml2::XMLElement* normalsAccNode = doc->NewElement("accessor");
      normalsTechNode->LinkEndChild(normalsAccNode);
      normalsAccNode->SetAttribute("source", avar("#%s-mesh-normals-array", lodMeshName));
      normalsAccNode->SetAttribute("count", avar("%d", exportData.detailLevels[d].mesh.mNormals.size()));
      normalsAccNode->SetAttribute("stride", "3");

      tinyxml2::XMLElement* normalsAccXNode = doc->NewElement("param");
      normalsAccNode->LinkEndChild(normalsAccXNode);
      normalsAccXNode->SetAttribute("name", "X");
      normalsAccXNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* normalsAccYNode = doc->NewElement("param");
      normalsAccNode->LinkEndChild(normalsAccYNode);
      normalsAccYNode->SetAttribute("name", "Y");
      normalsAccYNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* normalsAccZNode = doc->NewElement("param");
      normalsAccNode->LinkEndChild(normalsAccZNode);
      normalsAccZNode->SetAttribute("name", "Z");
      normalsAccZNode->SetAttribute("type", "float");

      // Save out the uvs
      tinyxml2::XMLElement* uv0SourceNode = doc->NewElement("source");
      meshNode->LinkEndChild(uv0SourceNode);
      uv0SourceNode->SetAttribute("id", avar("%s-mesh-map-0", lodMeshName));

      tinyxml2::XMLElement* uv0Node = doc->NewElement("float_array");
      uv0SourceNode->LinkEndChild(uv0Node);
      uv0Node->SetAttribute("id", avar("%s-mesh-map-0-array", lodMeshName));
      uv0Node->SetAttribute("count", avar("%d", exportData.detailLevels[d].mesh.mUV0s.size() * 2));

      for (U32 i = 0; i < exportData.detailLevels[d].mesh.mUV0s.size(); i++)
      {
         const Point2F& uv0 = exportData.detailLevels[d].mesh.mUV0s[i];

         tinyxml2::XMLText* uv0Text = doc->NewText(avar("%.4f %.4f ", uv0.x, 1.0f - uv0.y));   // COLLADA uvs are upside down compared to Torque
         uv0Node->LinkEndChild(uv0Text);
      }

      // Save the uv0 accessor
      tinyxml2::XMLElement* uv0TechNode = doc->NewElement("technique_common");
      uv0SourceNode->LinkEndChild(uv0TechNode);

      tinyxml2::XMLElement* uv0AccNode = doc->NewElement("accessor");
      uv0TechNode->LinkEndChild(uv0AccNode);
      uv0AccNode->SetAttribute("source", avar("#%s-mesh-map-0-array", lodMeshName));
      uv0AccNode->SetAttribute("count", avar("%d", exportData.detailLevels[d].mesh.mUV0s.size()));
      uv0AccNode->SetAttribute("stride", "2");

      tinyxml2::XMLElement* uv0AccSNode = doc->NewElement("param");
      uv0AccNode->LinkEndChild(uv0AccSNode);
      uv0AccSNode->SetAttribute("name", "S");
      uv0AccSNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* uv0AccTNode = doc->NewElement("param");
      uv0AccNode->LinkEndChild(uv0AccTNode);
      uv0AccTNode->SetAttribute("name", "T");
      uv0AccTNode->SetAttribute("type", "float");

      // Define the vertices position array
      tinyxml2::XMLElement* verticesNode = doc->NewElement("vertices");
      meshNode->LinkEndChild(verticesNode);
      verticesNode->SetAttribute("id", avar("%s-mesh-vertices", lodMeshName));

      tinyxml2::XMLElement* verticesInputNode = doc->NewElement("input");
      verticesNode->LinkEndChild(verticesInputNode);
      verticesInputNode->SetAttribute("semantic", "POSITION");
      verticesInputNode->SetAttribute("source", avar("#%s-mesh-positions", lodMeshName));

      Vector<String> mapNames;

      //exportColladaTriangles(meshNode, exportData.detailLevels[d].mesh, lodMeshName, mapNames);
      exportColladaTriangles(meshNode, exportData, d, lodMeshName);

      // Extra info useful for COLLADAMaya importer (OpenCOLLADA)
      tinyxml2::XMLElement* extraGeoNode = doc->NewElement("extra");
      libGeomsNode->LinkEndChild(extraGeoNode);

      tinyxml2::XMLElement* extraGeoNodeTech = doc->NewElement("technique");
      extraGeoNode->LinkEndChild(extraGeoNodeTech);
      extraGeoNodeTech->SetAttribute("profile", "OpenCOLLADAMaya");

      tinyxml2::XMLElement* mayaNode2Id = doc->NewElement("originalMayaNodeId");
      extraGeoNodeTech->LinkEndChild(mayaNode2Id);
      mayaNode2Id->SetAttribute("sid", "originalMayaNodeId");
      tinyxml2::XMLText* mayaIdMesh = doc->NewText(avar("%s", lodMeshName));
      mayaNode2Id->LinkEndChild(mayaIdMesh);

      tinyxml2::XMLElement* doubleSidedId = doc->NewElement("double_sided");
      extraGeoNodeTech->LinkEndChild(doubleSidedId);
      doubleSidedId->SetAttribute("sid", "double_sided");
      tinyxml2::XMLText* doubleSideIdText = doc->NewText("1");
      doubleSidedId->LinkEndChild(doubleSideIdText);

      tinyxml2::XMLElement* paramExtraNode = doc->NewElement("param");
      extraGeoNodeTech->LinkEndChild(paramExtraNode);
      paramExtraNode->SetAttribute("sid", "colladaId");
      paramExtraNode->SetAttribute("type", "string");

      tinyxml2::XMLText* mayaParamMesh = doc->NewText(avar("%s-mesh", lodMeshName));
      paramExtraNode->LinkEndChild(mayaParamMesh);
   }

   //And now collisions
   for (U32 d = 0; d < exportData.colMeshes.size(); d++)
   {
      const char* colMeshName = exportData.colMeshes[d].colMeshName;

      char colMeshId[256];
      dSprintf(colMeshId, 256, "%s-mesh", colMeshName);

      tinyxml2::XMLElement* geometryNode = doc->NewElement("geometry");
      libGeomsNode->LinkEndChild(geometryNode);
      geometryNode->SetAttribute("id", colMeshId);
      geometryNode->SetAttribute("name", colMeshName);

      tinyxml2::XMLElement* meshNode = doc->NewElement("mesh");
      geometryNode->LinkEndChild(meshNode);

      // Save out the vertices
      tinyxml2::XMLElement* vertsSourceNode = doc->NewElement("source");
      meshNode->LinkEndChild(vertsSourceNode);
      vertsSourceNode->SetAttribute("id", avar("%s-mesh-positions", colMeshName));

      tinyxml2::XMLElement* vertsNode = doc->NewElement("float_array");
      vertsSourceNode->LinkEndChild(vertsNode);
      vertsNode->SetAttribute("id", avar("%s-mesh-positions-array", colMeshName));
      vertsNode->SetAttribute("count", avar("%d", exportData.colMeshes[d].mesh.mPoints.size() * 3));

      for (U32 i = 0; i < exportData.colMeshes[d].mesh.mPoints.size(); i++)
      {
         const Point3F& vert = exportData.colMeshes[d].mesh.mPoints[i];

         tinyxml2::XMLText* vertText = doc->NewText(avar("%.4f %.4f %.4f ", vert.x, vert.y, vert.z));
         vertsNode->LinkEndChild(vertText);
      }

      // Save the vertex accessor
      tinyxml2::XMLElement* vertsTechNode = doc->NewElement("technique_common");
      vertsSourceNode->LinkEndChild(vertsTechNode);

      tinyxml2::XMLElement* vertsAccNode = doc->NewElement("accessor");
      vertsTechNode->LinkEndChild(vertsAccNode);
      vertsAccNode->SetAttribute("source", avar("#%s-mesh-positions-array", colMeshName));
      vertsAccNode->SetAttribute("count", avar("%d", exportData.colMeshes[d].mesh.mPoints.size()));
      vertsAccNode->SetAttribute("stride", "3");

      tinyxml2::XMLElement* vertsAccXNode = doc->NewElement("param");
      vertsAccNode->LinkEndChild(vertsAccXNode);
      vertsAccXNode->SetAttribute("name", "X");
      vertsAccXNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* vertsAccYNode = doc->NewElement("param");
      vertsAccNode->LinkEndChild(vertsAccYNode);
      vertsAccYNode->SetAttribute("name", "Y");
      vertsAccYNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* vertsAccZNode = doc->NewElement("param");
      vertsAccNode->LinkEndChild(vertsAccZNode);
      vertsAccZNode->SetAttribute("name", "Z");
      vertsAccZNode->SetAttribute("type", "float");

      // Save out the normals
      tinyxml2::XMLElement* normalsSourceNode = doc->NewElement("source");
      meshNode->LinkEndChild(normalsSourceNode);
      normalsSourceNode->SetAttribute("id", avar("%s-mesh-normals", colMeshName));

      tinyxml2::XMLElement* normalsNode = doc->NewElement("float_array");
      normalsSourceNode->LinkEndChild(normalsNode);
      normalsNode->SetAttribute("id", avar("%s-mesh-normals-array", colMeshName));
      normalsNode->SetAttribute("count", avar("%d", exportData.colMeshes[d].mesh.mNormals.size() * 3));

      for (U32 i = 0; i < exportData.colMeshes[d].mesh.mNormals.size(); i++)
      {
         const Point3F& normal = exportData.colMeshes[d].mesh.mNormals[i];

         tinyxml2::XMLText* normalText = doc->NewText(avar("%.4f %.4f %.4f ", normal.x, normal.y, normal.z));
         normalsNode->LinkEndChild(normalText);
      }

      // Save the normals accessor
      tinyxml2::XMLElement* normalsTechNode = doc->NewElement("technique_common");
      normalsSourceNode->LinkEndChild(normalsTechNode);

      tinyxml2::XMLElement* normalsAccNode = doc->NewElement("accessor");
      normalsTechNode->LinkEndChild(normalsAccNode);
      normalsAccNode->SetAttribute("source", avar("#%s-mesh-normals-array", colMeshName));
      normalsAccNode->SetAttribute("count", avar("%d", exportData.colMeshes[d].mesh.mNormals.size()));
      normalsAccNode->SetAttribute("stride", "3");

      tinyxml2::XMLElement* normalsAccXNode = doc->NewElement("param");
      normalsAccNode->LinkEndChild(normalsAccXNode);
      normalsAccXNode->SetAttribute("name", "X");
      normalsAccXNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* normalsAccYNode = doc->NewElement("param");
      normalsAccNode->LinkEndChild(normalsAccYNode);
      normalsAccYNode->SetAttribute("name", "Y");
      normalsAccYNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* normalsAccZNode = doc->NewElement("param");
      normalsAccNode->LinkEndChild(normalsAccZNode);
      normalsAccZNode->SetAttribute("name", "Z");
      normalsAccZNode->SetAttribute("type", "float");

      // Save out the uvs
      tinyxml2::XMLElement* uv0SourceNode = doc->NewElement("source");
      meshNode->LinkEndChild(uv0SourceNode);
      uv0SourceNode->SetAttribute("id", avar("%s-mesh-map-0", colMeshName));

      tinyxml2::XMLElement* uv0Node = doc->NewElement("float_array");
      uv0SourceNode->LinkEndChild(uv0Node);
      uv0Node->SetAttribute("id", avar("%s-mesh-map-0-array", colMeshName));
      uv0Node->SetAttribute("count", avar("%d", exportData.colMeshes[d].mesh.mUV0s.size() * 2));

      for (U32 i = 0; i < exportData.colMeshes[d].mesh.mUV0s.size(); i++)
      {
         const Point2F& uv0 = exportData.colMeshes[d].mesh.mUV0s[i];

         tinyxml2::XMLText* uv0Text = doc->NewText(avar("%.4f %.4f ", uv0.x, 1.0f - uv0.y));   // COLLADA uvs are upside down compared to Torque
         uv0Node->LinkEndChild(uv0Text);
      }

      // Save the uv0 accessor
      tinyxml2::XMLElement* uv0TechNode = doc->NewElement("technique_common");
      uv0SourceNode->LinkEndChild(uv0TechNode);

      tinyxml2::XMLElement* uv0AccNode = doc->NewElement("accessor");
      uv0TechNode->LinkEndChild(uv0AccNode);
      uv0AccNode->SetAttribute("source", avar("#%s-mesh-map-0-array", colMeshName));
      uv0AccNode->SetAttribute("count", avar("%d", exportData.colMeshes[d].mesh.mUV0s.size()));
      uv0AccNode->SetAttribute("stride", "2");

      tinyxml2::XMLElement* uv0AccSNode = doc->NewElement("param");
      uv0AccNode->LinkEndChild(uv0AccSNode);
      uv0AccSNode->SetAttribute("name", "S");
      uv0AccSNode->SetAttribute("type", "float");

      tinyxml2::XMLElement* uv0AccTNode = doc->NewElement("param");
      uv0AccNode->LinkEndChild(uv0AccTNode);
      uv0AccTNode->SetAttribute("name", "T");
      uv0AccTNode->SetAttribute("type", "float");

      // Define the vertices position array
      tinyxml2::XMLElement* verticesNode = doc->NewElement("vertices");
      meshNode->LinkEndChild(verticesNode);
      verticesNode->SetAttribute("id", avar("%s-mesh-vertices", colMeshName));

      tinyxml2::XMLElement* verticesInputNode = doc->NewElement("input");
      verticesNode->LinkEndChild(verticesInputNode);
      verticesInputNode->SetAttribute("semantic", "POSITION");
      verticesInputNode->SetAttribute("source", avar("#%s-mesh-positions", colMeshName));

      Vector<String> mapNames;

      //exportColladaTriangles(meshNode, exportData.detailLevels[d].mesh, lodMeshName, mapNames);
      exportColladaCollisionTriangles(meshNode, exportData, d);

      // Extra info useful for COLLADAMaya importer (OpenCOLLADA)
      tinyxml2::XMLElement* extraGeoNode = doc->NewElement("extra");
      libGeomsNode->LinkEndChild(extraGeoNode);

      tinyxml2::XMLElement* extraGeoNodeTech = doc->NewElement("technique");
      extraGeoNode->LinkEndChild(extraGeoNodeTech);
      extraGeoNodeTech->SetAttribute("profile", "OpenCOLLADAMaya");

      tinyxml2::XMLElement* mayaNode2Id = doc->NewElement("originalMayaNodeId");
      extraGeoNodeTech->LinkEndChild(mayaNode2Id);
      mayaNode2Id->SetAttribute("sid", "originalMayaNodeId");
      tinyxml2::XMLText* mayaIdMesh = doc->NewText(avar("%s", colMeshName));
      mayaNode2Id->LinkEndChild(mayaIdMesh);

      tinyxml2::XMLElement* doubleSidedId = doc->NewElement("double_sided");
      extraGeoNodeTech->LinkEndChild(doubleSidedId);
      doubleSidedId->SetAttribute("sid", "double_sided");
      tinyxml2::XMLText* doubleSideIdText = doc->NewText("1");
      doubleSidedId->LinkEndChild(doubleSideIdText);

      tinyxml2::XMLElement* paramExtraNode = doc->NewElement("param");
      extraGeoNodeTech->LinkEndChild(paramExtraNode);
      paramExtraNode->SetAttribute("sid", "colladaId");
      paramExtraNode->SetAttribute("type", "string");

      tinyxml2::XMLText* mayaParamMesh = doc->NewText(avar("%s-mesh", colMeshName));
      paramExtraNode->LinkEndChild(mayaParamMesh);
   }
}

void ColladaUtils::exportColladaScene(tinyxml2::XMLElement* rootNode, const String& meshName, const Vector<String>& matNames)
{
   tinyxml2::XMLDocument* doc = rootNode->GetDocument();

   tinyxml2::XMLElement* libSceneNode = doc->NewElement("library_visual_scenes");
   rootNode->LinkEndChild(libSceneNode);

   tinyxml2::XMLElement* visSceneNode = doc->NewElement("visual_scene");
   libSceneNode->LinkEndChild(visSceneNode);
   visSceneNode->SetAttribute("id", "RootNode");
   visSceneNode->SetAttribute("name", "RootNode");

   tinyxml2::XMLElement* nodeNode = doc->NewElement("node");
   visSceneNode->LinkEndChild(nodeNode);
   nodeNode->SetAttribute("id", avar("%s", meshName.c_str()));
   nodeNode->SetAttribute("name", avar("%s", meshName.c_str()));
   nodeNode->SetAttribute("type", "NODE");
   
   tinyxml2::XMLElement* instanceGeomNode = doc->NewElement("instance_geometry");
   nodeNode->LinkEndChild(instanceGeomNode);
   instanceGeomNode->SetAttribute("url", avar("#%s-mesh", meshName.c_str()));
   instanceGeomNode->SetAttribute("name", avar("%s", meshName.c_str()));
   
   tinyxml2::XMLElement* bindMatNode = doc->NewElement("bind_material");
   instanceGeomNode->LinkEndChild(bindMatNode);

   tinyxml2::XMLElement* techniqueNode = doc->NewElement("technique_common");
   bindMatNode->LinkEndChild(techniqueNode);

   // Bind the materials
   for (U32 i = 0; i < matNames.size(); i++)
   {
      tinyxml2::XMLElement* instMatNode = doc->NewElement("instance_material");
      techniqueNode->LinkEndChild(instMatNode);
      instMatNode->SetAttribute("symbol", avar("%s-material", matNames[i].c_str()));
      instMatNode->SetAttribute("target", avar("#%s-material", matNames[i].c_str()));
      tinyxml2::XMLElement* bindVertexNode = doc->NewElement("bind_vertex_input");
      instMatNode->LinkEndChild(bindVertexNode);
      //bindVertexNode->SetAttribute("semantic", avar("%s-mesh-map-0", meshName.c_str()));
      bindVertexNode->SetAttribute("semantic", "UVMap");
      bindVertexNode->SetAttribute("input_semantic", "TEXCOORD");
      bindVertexNode->SetAttribute("input_set", "0"); 
    }
   
	// Extra info useful for COLLADAMax importer (OpenCOLLADA)
    tinyxml2::XMLElement* extraInsGeoNode = doc->NewElement("extra");
    nodeNode->LinkEndChild(extraInsGeoNode);

    tinyxml2::XMLElement* extraInsGeoTechNode = doc->NewElement("technique");
    extraInsGeoNode->LinkEndChild(extraInsGeoTechNode);
    extraInsGeoTechNode->SetAttribute("profile", "OpenCOLLADA");

    tinyxml2::XMLElement* castShadowsNode = doc->NewElement("cast_shadows");
    extraInsGeoTechNode->LinkEndChild(castShadowsNode);
    castShadowsNode->SetAttribute("sid", "cast_shadows");
    castShadowsNode->SetAttribute("type", "bool");
	
	tinyxml2::XMLText* castShadowsText = doc->NewText("1");
	castShadowsNode->LinkEndChild(castShadowsText);
	
	//-----------------------------
    tinyxml2::XMLElement* receiveShadowsNode = doc->NewElement("receive_shadows");
    extraInsGeoTechNode->LinkEndChild(receiveShadowsNode);
    receiveShadowsNode->SetAttribute("sid", "receive_shadows");
    receiveShadowsNode->SetAttribute("type", "bool");
	
	tinyxml2::XMLText* receiveShadowsText = doc->NewText("1");
	receiveShadowsNode->LinkEndChild(receiveShadowsText);	
	
	//-----------------------------
    tinyxml2::XMLElement* primaryVisibiltyNode = doc->NewElement("primary_visibility");
    extraInsGeoTechNode->LinkEndChild(primaryVisibiltyNode);
    primaryVisibiltyNode->SetAttribute("sid", "primary_visibility");
    primaryVisibiltyNode->SetAttribute("type", "int");
	
	tinyxml2::XMLText* primaryVisibiltyText = doc->NewText("1");
	primaryVisibiltyNode->LinkEndChild(primaryVisibiltyText);

	//-----------------------------
    tinyxml2::XMLElement* secondaryVisibilityNode = doc->NewElement("secondary_visibility");
    extraInsGeoTechNode->LinkEndChild(secondaryVisibilityNode);
    secondaryVisibilityNode->SetAttribute("sid", "secondary_visibility");
    secondaryVisibilityNode->SetAttribute("type", "int");
	
	tinyxml2::XMLText* secondaryVisibilityText = doc->NewText("1");
	secondaryVisibilityNode->LinkEndChild(secondaryVisibilityText);	

	// Extra info useful for COLLADAMaya importer (OpenCOLLADA)
    tinyxml2::XMLElement* extra2InsGeoNode = doc->NewElement("extra");
    nodeNode->LinkEndChild(extra2InsGeoNode);

    tinyxml2::XMLElement* extra2InsGeoTechNode = doc->NewElement("technique");
    extra2InsGeoNode->LinkEndChild(extra2InsGeoTechNode);
    extra2InsGeoTechNode->SetAttribute("profile", "OpenCOLLADAMaya");

    tinyxml2::XMLElement* mayaNodeId = doc->NewElement("originalMayaNodeId");
    extra2InsGeoTechNode->LinkEndChild(mayaNodeId);
    mayaNodeId->SetAttribute("sid", "originalMayaNodeId");
    mayaNodeId->SetAttribute("type", "string");

	tinyxml2::XMLText* mayaNodeIdMesh = doc->NewText(avar("%s", meshName.c_str()));
	mayaNodeId->LinkEndChild(mayaNodeIdMesh);	

    tinyxml2::XMLElement* paramExtraNode = doc->NewElement("param");
    extra2InsGeoTechNode->LinkEndChild(paramExtraNode);
    paramExtraNode->SetAttribute("sid", "colladaId");
    paramExtraNode->SetAttribute("type", "string");	

	tinyxml2::XMLText* mayaParamMesh = doc->NewText(avar("%s", meshName.c_str()));
	paramExtraNode->LinkEndChild(mayaParamMesh);	
	
	//-----------------------------
	
   tinyxml2::XMLElement* sceneNode = doc->NewElement("scene");
   rootNode->LinkEndChild(sceneNode);

   tinyxml2::XMLElement* instVisSceneNode = doc->NewElement("instance_visual_scene");
   sceneNode->LinkEndChild(instVisSceneNode);
   instVisSceneNode->SetAttribute("url", "#RootNode");
}

void ColladaUtils::exportColladaScene(tinyxml2::XMLElement* rootNode, const ExportData& exportData, const String& meshName)
{
   tinyxml2::XMLDocument* doc = rootNode->GetDocument();

   tinyxml2::XMLElement* libSceneNode = doc->NewElement("library_visual_scenes");
   rootNode->LinkEndChild(libSceneNode);

   tinyxml2::XMLElement* visSceneNode = doc->NewElement("visual_scene");
   libSceneNode->LinkEndChild(visSceneNode);
   visSceneNode->SetAttribute("id", "RootNode");
   visSceneNode->SetAttribute("name", "RootNode");

   for (U32 d = 0; d < exportData.detailLevels.size(); d++)
   {
      char lodMeshName[256];
      dSprintf(lodMeshName, 256, "%s%d", meshName.c_str(), exportData.detailLevels[d].size);

      tinyxml2::XMLElement* nodeNode = doc->NewElement("node");
      visSceneNode->LinkEndChild(nodeNode);
      nodeNode->SetAttribute("id", lodMeshName);
      nodeNode->SetAttribute("name", lodMeshName);
      nodeNode->SetAttribute("type", "NODE");

      tinyxml2::XMLElement* instanceGeomNode = doc->NewElement("instance_geometry");
      nodeNode->LinkEndChild(instanceGeomNode);
      instanceGeomNode->SetAttribute("url", avar("#%s%d-mesh", meshName.c_str(), exportData.detailLevels[d].size));
      instanceGeomNode->SetAttribute("name", lodMeshName);

      tinyxml2::XMLElement* bindMatNode = doc->NewElement("bind_material");
      instanceGeomNode->LinkEndChild(bindMatNode);

      tinyxml2::XMLElement* techniqueNode = doc->NewElement("technique_common");
      bindMatNode->LinkEndChild(techniqueNode);

      // Bind the materials
      for (U32 i = 0; i < exportData.detailLevels[d].materialRefList.size(); i++)
      {
         int matIdx;
         exportData.detailLevels[d].materialRefList.tryGetValue(i, matIdx);
         BaseMatInstance* baseInst = exportData.materials[matIdx];

         Material* mat = dynamic_cast<Material*>(baseInst->getMaterial());
         if (!mat)
            continue;

         String matName;

         if (mat->getName() && mat->getName()[0])
            matName = mat->mMapTo;

         tinyxml2::XMLElement* instMatNode = doc->NewElement("instance_material");
         techniqueNode->LinkEndChild(instMatNode);
         instMatNode->SetAttribute("symbol", avar("%s-material", matName.c_str()));
         instMatNode->SetAttribute("target", avar("#%s-material", matName.c_str()));
         tinyxml2::XMLElement* bindVertexNode = doc->NewElement("bind_vertex_input");
         instMatNode->LinkEndChild(bindVertexNode);
         //bindVertexNode->SetAttribute("semantic", avar("%s-mesh-map-0", meshName.c_str()));
         bindVertexNode->SetAttribute("semantic", "UVMap");
         bindVertexNode->SetAttribute("input_semantic", "TEXCOORD");
         bindVertexNode->SetAttribute("input_set", "0");
      }

      // Extra info useful for COLLADAMax importer (OpenCOLLADA)
      tinyxml2::XMLElement* extraInsGeoNode = doc->NewElement("extra");
      nodeNode->LinkEndChild(extraInsGeoNode);

      tinyxml2::XMLElement* extraInsGeoTechNode = doc->NewElement("technique");
      extraInsGeoNode->LinkEndChild(extraInsGeoTechNode);
      extraInsGeoTechNode->SetAttribute("profile", "OpenCOLLADA");

      tinyxml2::XMLElement* castShadowsNode = doc->NewElement("cast_shadows");
      extraInsGeoTechNode->LinkEndChild(castShadowsNode);
      castShadowsNode->SetAttribute("sid", "cast_shadows");
      castShadowsNode->SetAttribute("type", "bool");

      tinyxml2::XMLText* castShadowsText = doc->NewText("1");
      castShadowsNode->LinkEndChild(castShadowsText);

      //-----------------------------
      tinyxml2::XMLElement* receiveShadowsNode = doc->NewElement("receive_shadows");
      extraInsGeoTechNode->LinkEndChild(receiveShadowsNode);
      receiveShadowsNode->SetAttribute("sid", "receive_shadows");
      receiveShadowsNode->SetAttribute("type", "bool");

      tinyxml2::XMLText* receiveShadowsText = doc->NewText("1");
      receiveShadowsNode->LinkEndChild(receiveShadowsText);

      //-----------------------------
      tinyxml2::XMLElement* primaryVisibiltyNode = doc->NewElement("primary_visibility");
      extraInsGeoTechNode->LinkEndChild(primaryVisibiltyNode);
      primaryVisibiltyNode->SetAttribute("sid", "primary_visibility");
      primaryVisibiltyNode->SetAttribute("type", "int");

      tinyxml2::XMLText* primaryVisibiltyText = doc->NewText("1");
      primaryVisibiltyNode->LinkEndChild(primaryVisibiltyText);

      //-----------------------------
      tinyxml2::XMLElement* secondaryVisibilityNode = doc->NewElement("secondary_visibility");
      extraInsGeoTechNode->LinkEndChild(secondaryVisibilityNode);
      secondaryVisibilityNode->SetAttribute("sid", "secondary_visibility");
      secondaryVisibilityNode->SetAttribute("type", "int");

      tinyxml2::XMLText* secondaryVisibilityText = doc->NewText("1");
      secondaryVisibilityNode->LinkEndChild(secondaryVisibilityText);

      // Extra info useful for COLLADAMaya importer (OpenCOLLADA)
      tinyxml2::XMLElement* extra2InsGeoNode = doc->NewElement("extra");
      nodeNode->LinkEndChild(extra2InsGeoNode);

      tinyxml2::XMLElement* extra2InsGeoTechNode = doc->NewElement("technique");
      extra2InsGeoNode->LinkEndChild(extra2InsGeoTechNode);
      extra2InsGeoTechNode->SetAttribute("profile", "OpenCOLLADAMaya");

      tinyxml2::XMLElement* mayaNodeId = doc->NewElement("originalMayaNodeId");
      extra2InsGeoTechNode->LinkEndChild(mayaNodeId);
      mayaNodeId->SetAttribute("sid", "originalMayaNodeId");
      mayaNodeId->SetAttribute("type", "string");

      tinyxml2::XMLText* mayaNodeIdMesh = doc->NewText(lodMeshName);
      mayaNodeId->LinkEndChild(mayaNodeIdMesh);

      tinyxml2::XMLElement* paramExtraNode = doc->NewElement("param");
      extra2InsGeoTechNode->LinkEndChild(paramExtraNode);
      paramExtraNode->SetAttribute("sid", "colladaId");
      paramExtraNode->SetAttribute("type", "string");

      tinyxml2::XMLText* mayaParamMesh = doc->NewText(lodMeshName);
      paramExtraNode->LinkEndChild(mayaParamMesh);
   }

   //Collisions
   for (U32 d = 0; d < exportData.colMeshes.size(); d++)
   {
      const char* colMeshName = exportData.colMeshes[d].colMeshName;

      tinyxml2::XMLElement* nodeNode = doc->NewElement("node");
      visSceneNode->LinkEndChild(nodeNode);
      nodeNode->SetAttribute("id", colMeshName);
      nodeNode->SetAttribute("name", colMeshName);
      nodeNode->SetAttribute("type", "NODE");

      tinyxml2::XMLElement* instanceGeomNode = doc->NewElement("instance_geometry");
      nodeNode->LinkEndChild(instanceGeomNode);
      instanceGeomNode->SetAttribute("url", avar("#%s-mesh", colMeshName));
      instanceGeomNode->SetAttribute("name", colMeshName);

      tinyxml2::XMLElement* bindMatNode = doc->NewElement("bind_material");
      instanceGeomNode->LinkEndChild(bindMatNode);

      tinyxml2::XMLElement* techniqueNode = doc->NewElement("technique_common");
      bindMatNode->LinkEndChild(techniqueNode);

      tinyxml2::XMLElement* instMatNode = doc->NewElement("instance_material");
      techniqueNode->LinkEndChild(instMatNode);
      instMatNode->SetAttribute("symbol", avar("%s-material", colMeshName));
      instMatNode->SetAttribute("target", avar("#%s-material", colMeshName));
      tinyxml2::XMLElement* bindVertexNode = doc->NewElement("bind_vertex_input");
      instMatNode->LinkEndChild(bindVertexNode);
      //bindVertexNode->SetAttribute("semantic", avar("%s-mesh-map-0", meshName.c_str()));
      bindVertexNode->SetAttribute("semantic", "UVMap");
      bindVertexNode->SetAttribute("input_semantic", "TEXCOORD");
      bindVertexNode->SetAttribute("input_set", "0");

      // Extra info useful for COLLADAMax importer (OpenCOLLADA)
      tinyxml2::XMLElement* extraInsGeoNode = doc->NewElement("extra");
      nodeNode->LinkEndChild(extraInsGeoNode);

      tinyxml2::XMLElement* extraInsGeoTechNode = doc->NewElement("technique");
      extraInsGeoNode->LinkEndChild(extraInsGeoTechNode);
      extraInsGeoTechNode->SetAttribute("profile", "OpenCOLLADA");

      tinyxml2::XMLElement* castShadowsNode = doc->NewElement("cast_shadows");
      extraInsGeoTechNode->LinkEndChild(castShadowsNode);
      castShadowsNode->SetAttribute("sid", "cast_shadows");
      castShadowsNode->SetAttribute("type", "bool");

      tinyxml2::XMLText* castShadowsText = doc->NewText("1");
      castShadowsNode->LinkEndChild(castShadowsText);

      //-----------------------------
      tinyxml2::XMLElement* receiveShadowsNode = doc->NewElement("receive_shadows");
      extraInsGeoTechNode->LinkEndChild(receiveShadowsNode);
      receiveShadowsNode->SetAttribute("sid", "receive_shadows");
      receiveShadowsNode->SetAttribute("type", "bool");

      tinyxml2::XMLText* receiveShadowsText = doc->NewText("1");
      receiveShadowsNode->LinkEndChild(receiveShadowsText);

      //-----------------------------
      tinyxml2::XMLElement* primaryVisibiltyNode = doc->NewElement("primary_visibility");
      extraInsGeoTechNode->LinkEndChild(primaryVisibiltyNode);
      primaryVisibiltyNode->SetAttribute("sid", "primary_visibility");
      primaryVisibiltyNode->SetAttribute("type", "int");

      tinyxml2::XMLText* primaryVisibiltyText = doc->NewText("1");
      primaryVisibiltyNode->LinkEndChild(primaryVisibiltyText);

      //-----------------------------
      tinyxml2::XMLElement* secondaryVisibilityNode = doc->NewElement("secondary_visibility");
      extraInsGeoTechNode->LinkEndChild(secondaryVisibilityNode);
      secondaryVisibilityNode->SetAttribute("sid", "secondary_visibility");
      secondaryVisibilityNode->SetAttribute("type", "int");

      tinyxml2::XMLText* secondaryVisibilityText = doc->NewText("1");
      secondaryVisibilityNode->LinkEndChild(secondaryVisibilityText);

      // Extra info useful for COLLADAMaya importer (OpenCOLLADA)
      tinyxml2::XMLElement* extra2InsGeoNode = doc->NewElement("extra");
      nodeNode->LinkEndChild(extra2InsGeoNode);

      tinyxml2::XMLElement* extra2InsGeoTechNode = doc->NewElement("technique");
      extra2InsGeoNode->LinkEndChild(extra2InsGeoTechNode);
      extra2InsGeoTechNode->SetAttribute("profile", "OpenCOLLADAMaya");

      tinyxml2::XMLElement* mayaNodeId = doc->NewElement("originalMayaNodeId");
      extra2InsGeoTechNode->LinkEndChild(mayaNodeId);
      mayaNodeId->SetAttribute("sid", "originalMayaNodeId");
      mayaNodeId->SetAttribute("type", "string");

      tinyxml2::XMLText* mayaNodeIdMesh = doc->NewText(colMeshName);
      mayaNodeId->LinkEndChild(mayaNodeIdMesh);

      tinyxml2::XMLElement* paramExtraNode = doc->NewElement("param");
      extra2InsGeoTechNode->LinkEndChild(paramExtraNode);
      paramExtraNode->SetAttribute("sid", "colladaId");
      paramExtraNode->SetAttribute("type", "string");

      tinyxml2::XMLText* mayaParamMesh = doc->NewText(colMeshName);
      paramExtraNode->LinkEndChild(mayaParamMesh);
   }
   //-----------------------------

   tinyxml2::XMLElement* sceneNode = doc->NewElement("scene");
   rootNode->LinkEndChild(sceneNode);

   tinyxml2::XMLElement* instVisSceneNode = doc->NewElement("instance_visual_scene");
   sceneNode->LinkEndChild(instVisSceneNode);
   instVisSceneNode->SetAttribute("url", "#RootNode");
}

void ColladaUtils::exportToCollada(const Torque::Path& colladaFile, const OptimizedPolyList& mesh, const String& meshName)
{
   // Get the mesh name
   String outMeshName = meshName;

   if (outMeshName.isEmpty())
      outMeshName = colladaFile.getFileName();

   // The XML document that will hold all of our data
   VfsXMLDocument doc;

   // Add a standard XML declaration to the top
   tinyxml2::XMLDeclaration* xmlDecl = doc.NewDeclaration();
   doc.LinkEndChild(xmlDecl);

   // Create our Collada root node and populate a couple standard attributes
   tinyxml2::XMLElement* rootNode = doc.NewElement("COLLADA");
   rootNode->SetAttribute("xmlns", "http://www.collada.org/2005/11/COLLADASchema");
   rootNode->SetAttribute("version", "1.4.1");
   //rootNode->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance"); //T3D Collada loader complaint about this.
   

   // Add the root node to the document
   doc.LinkEndChild(rootNode);

   // Save out our header info
   exportColladaHeader(rootNode);

   // Save out the materials
   Vector<String> mapNames;

   exportColladaMaterials(rootNode, mesh, mapNames, colladaFile);

   S32 suffix;
   String baseMeshName = String::GetTrailingNumber(outMeshName, suffix);

   // Save out our geometry
   exportColladaMesh(rootNode, mesh, baseMeshName, mapNames);

   // Save out our scene nodes
   exportColladaScene(rootNode, baseMeshName, mapNames);

   // Write out the actual Collada file
   char fullPath[MAX_PATH_LENGTH];
   Platform::makeFullPathName(colladaFile.getFullPath(), fullPath, MAX_PATH_LENGTH);

   if (!doc.SaveFile(fullPath))
      Con::errorf("ColladaUtils::exportToCollada(): Unable to export to %s", fullPath);
}

void ColladaUtils::exportToCollada(const Torque::Path& colladaFile, const ExportData& exportData)
{
   // Get the mesh name
   String outMeshName = colladaFile.getFileName();

   // The XML document that will hold all of our data
   VfsXMLDocument doc;

   // Add a standard XML declaration to the top
   tinyxml2::XMLDeclaration* xmlDecl = doc.NewDeclaration();
   doc.LinkEndChild(xmlDecl);

   // Create our Collada root node and populate a couple standard attributes
   tinyxml2::XMLElement* rootNode = doc.NewElement("COLLADA");
   rootNode->SetAttribute("xmlns", "http://www.collada.org/2005/11/COLLADASchema");
   rootNode->SetAttribute("version", "1.4.1");
   //rootNode->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance"); //T3D Collada loader complaint about this.


   // Add the root node to the document
   doc.LinkEndChild(rootNode);

   // Save out our header info
   exportColladaHeader(rootNode);

   // Save out the materials
   Vector<String> mapNames;

   exportColladaMaterials(rootNode, exportData, colladaFile);

   S32 suffix;
   String baseMeshName = String::GetTrailingNumber(outMeshName, suffix);

   // Save out our geometry
   exportColladaMesh(rootNode, exportData, baseMeshName);

   // Save out our scene nodes
   exportColladaScene(rootNode, exportData, baseMeshName);

   // Write out the actual Collada file
   char fullPath[MAX_PATH_LENGTH];
   Platform::makeFullPathName(colladaFile.getFullPath(), fullPath, MAX_PATH_LENGTH);

   if (!doc.SaveFile(fullPath))
      Con::errorf("ColladaUtils::exportToCollada(): Unable to export to %s", fullPath);
}

void ColladaUtils::ExportData::processData()
{
   //This pref dictates if we 'backfill' lower LODs with higher ones if any given mesh being exported lacks that level.
   //For example, if there are 2 meshes, and one has 500, 200, 100 and the other has 500 and 200 - if this setting is on, the second mesh
   //will backfill the 200 to the 100 so it has all levels filled. If it's off, the second mesh will not render at the 100 level
   bool fillLowDetailLevels = dAtob(Con::getVariable("$exportMesh::fillLowDetailLevels", "1"));

   S32 numDetailLevels = numberOfDetailLevels();

   detailLevels.clear();
   detailLevels.setSize(numDetailLevels);

   for (U32 meshNum = 0; meshNum < meshData.size(); ++meshNum)
   {
      for (U32 i = 0; i < numDetailLevels; ++i)
      {
         //Get our target size
         S32 targetDetailLevelSize = getDetailLevelSize(i);

         //alright, step through each meshdata and propagate the polyList info 'up' to fill 
         detailLevel* curDetail = &detailLevels[i];

         curDetail->size = targetDetailLevelSize;
      
         //Do we have a detail level for this?
         S32 detailLevelIdx = -1;

         for (S32 mdl = i; mdl >= 0; mdl--)
         {
            //walk backwards as needed to find our first valid detail level for this mesh. if we find none, just move on
            S32 testDetailLevelSize = getDetailLevelSize(mdl);
            detailLevelIdx = meshData[meshNum].hasDetailLevel(testDetailLevelSize);

            if (detailLevelIdx != -1)
               break;
         }

         if (detailLevelIdx == -1)
         {
            //found nothing backwards, so lets check if we're configured to back-fill the first detail levels
            if (fillLowDetailLevels)
            {
               //if so, search forward, find the first valid detail and fill it in
               for (S32 mdl = 0; mdl < numDetailLevels; mdl++)
               {
                  //walk backwards as needed to find our first valid detail level for this mesh. if we find none, just move on
                  S32 testDetailLevelSize = getDetailLevelSize(mdl);
                  detailLevelIdx = meshData[meshNum].hasDetailLevel(testDetailLevelSize);

                  if (detailLevelIdx != -1)
                     break;
               }
            }
         }

         //If we found the detail level index, go ahead and build out the data for it
         if (detailLevelIdx != -1)
         {
            curDetail->mesh.setTransform(&meshData[meshNum].meshTransform, meshData[meshNum].scale);
            curDetail->mesh.setObject(meshData[meshNum].originatingObject);

            if (meshData[meshNum].shapeInst != nullptr)
            {

               if (!meshData[meshNum].shapeInst->buildPolyList(&curDetail->mesh, detailLevelIdx))
               {
                  Con::errorf("TSStatic::buildExportPolyList - failed to build polylist for LOD %i", i);
                  continue;
               }
            }
            else
            {
               //special handling classes
               ConvexShape* convexShp = dynamic_cast<ConvexShape*>(meshData[meshNum].originatingObject);
               if (convexShp != nullptr)
               {
                  if (!convexShp->buildPolyList(PLC_Export, &curDetail->mesh, meshData[meshNum].originatingObject->getWorldBox(), meshData[meshNum].originatingObject->getWorldSphere()))
                  {
                     Con::errorf("TSStatic::buildExportPolyList - failed to build ConvexShape polylist for LOD %i", i);
                     continue;
                  }
               }
            }

            //lastly, get material
            for (U32 matNum = 0; matNum < curDetail->mesh.mMaterialList.size(); matNum++)
            {
               S32 matIdx = hasMaterialInstance(curDetail->mesh.mMaterialList[matNum]);

               if (matIdx == -1)
               {
                  //cool, haven't already got this material, so lets store it out
                  materials.push_back(curDetail->mesh.mMaterialList[matNum]);
                  curDetail->materialRefList.insert(matNum, materials.size() - 1);
               }
               else
               {
                  curDetail->materialRefList.insert(matNum, matIdx);
               }
            }
         }
      }
   }
}
