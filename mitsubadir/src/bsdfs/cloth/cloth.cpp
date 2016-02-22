// vim makeprg=scons
/*
   This file is part of Mitsuba, a physically based rendering system.

   Copyright (c) 2007-2014 by Wenzel Jakob and others.

   Mitsuba is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License Version 3
   as published by the Free Software Foundation.

   Mitsuba is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <mitsuba/render/scene.h>
#include <mitsuba/render/bsdf.h>
#include <mitsuba/render/texture.h>
#include <mitsuba/hw/basicshader.h>
#include <mitsuba/core/warp.h>
#include <mitsuba/core/fresolver.h>
#include "wif/wif.c"
#include "wif/ini.c" //TODO Snygga till! (använda scons?!)

//TODO(Vidar): Enable floating point exceptions

MTS_NAMESPACE_BEGIN

//TODO(Vidar): Write documentation

/*!\plugin{diffuse_copy}{Smooth diffuse material}
 * \order{1}
 * \icon{bsdf_diffuse}
 * \parameters{
 *     \parameter{reflectance}{\Spectrum\Or\Texture}{
 *       Specifies the diffuse albedo of the
 *       material \default{0.5}
 *     }
 * }
 *
 * \renderings{
 *     \rendering{Homogeneous reflectance, see \lstref{diffuse-uniform}}
 *         {bsdf_diffuse_plain}
 *     \rendering{Textured reflectance, see \lstref{diffuse-textured}}
 *         {bsdf_diffuse_textured}
 * }
 *
 * The smooth diffuse material (also referred to as ``Lambertian'')
 * represents an ideally diffuse material with a user-specified amount of
 * reflectance. Any received illumination is scattered so that the surface
 * looks the same independently of the direction of observation.
 *
 * Apart from a  homogeneous reflectance value, the plugin can also accept
 * a nested or referenced texture map to be used as the source of reflectance
 * information, which is then mapped onto the shape based on its UV
 * parameterization. When no parameters are specified, the model uses the default
 * of 50% reflectance.
 *
 * Note that this material is one-sided---that is, observed from the
 * back side, it will be completely black. If this is undesirable,
 * consider using the \pluginref{twosided} BRDF adapter plugin.
 * \vspace{4mm}
 *
 * \begin{xml}[caption={A diffuse material, whose reflectance is specified
 *     as an sRGB color}, label=lst:diffuse-uniform]
 * <bsdf type="diffuse">
 *     <srgb name="reflectance" value="#6d7185"/>
 * </bsdf>
 * \end{xml}
 *
 * \begin{xml}[caption=A diffuse material with a texture map,
 *     label=lst:diffuse-textured]
 * <bsdf type="diffuse">
 *     <texture type="bitmap" name="reflectance">
 *         <string name="filename" value="wood.jpg"/>
 *     </texture>
 * </bsdf>
 * \end{xml}
 */
class Cloth : public BSDF {
    public:
        PaletteEntry * m_pattern_entry;
        uint32_t m_pattern_height;
        uint32_t m_pattern_width;
        float m_umax;
        float m_uscale;
        float m_vscale;
        Cloth(const Properties &props)
            : BSDF(props) {
                /* For better compatibility with other models, support both
                   'reflectance' and 'diffuseReflectance' as parameter names */
                m_umax = props.getFloat("umax", 0.7f);
                m_uscale = props.getFloat("utiling", 1.0f);
                m_vscale = props.getFloat("vtiling", 1.0f);

                // LOAD WIF FILE
                std::string wiffilename =
                    Thread::getThread()->getFileResolver()->
                    resolve(props.getString("wiffile")).string();
                const char *filename = wiffilename.c_str();
                WeaveData *data = wif_read(filename);

                m_pattern_entry = wif_get_pattern(data,&m_pattern_height,
                         &m_pattern_width);
                wif_free_weavedata(data);

            }

        Cloth(Stream *stream, InstanceManager *manager)
            : BSDF(stream, manager) {
                //TODO(Vidar):Read parameters from stream

                configure();
            }
        ~Cloth() {
            wif_free_pattern(m_pattern_entry);
        }

        void configure() {
            /* Verify the input parameter and fix them if necessary */
            m_components.clear();
            m_components.push_back(EDiffuseReflection | EFrontSide
                    | ESpatiallyVarying);
            m_usesRayDifferentials = true;

            BSDF::configure();
        }

        Spectrum getDiffuseReflectance(const Intersection &its) const {
            PatternData pattern_data = getPatternData(its);
            return pattern_data.color;
        }

        struct PatternData
        {
            Spectrum color;
            Frame frame;
            float u, v; //Segment uv coordinates (in angles)
            float x, y; //position within segment. 
            bool warp_above; 
        };

        void calculateLengthOfSegment(bool warp_above, uint32_t pattern_x,
                uint32_t pattern_y, uint32_t *steps_left,
                uint32_t *steps_right) const
        {

            uint32_t current_x = pattern_x;
            uint32_t current_y = pattern_y;
            uint32_t *incremented_coord = warp_above ? &current_y : &current_x;
            uint32_t max_size = warp_above ? m_pattern_height: m_pattern_width;
            uint32_t initial_coord = warp_above ? pattern_y: pattern_x;
            *steps_right = 0;
            *steps_left  = 0;
            do{
                (*incremented_coord)++;
                if(*incremented_coord == max_size){
                    *incremented_coord = 0;
                }
                if(m_pattern_entry[current_x +
                        current_y*m_pattern_width].warp_above != warp_above){
                    break;
                }
                (*steps_right)++;
            } while(*incremented_coord != initial_coord);

            *incremented_coord = initial_coord;
            do{
                if(*incremented_coord == 0){
                    *incremented_coord = max_size;
                }
                (*incremented_coord)--;
                if(m_pattern_entry[current_x +
                        current_y*m_pattern_width].warp_above != warp_above){
                    break;
                }
                (*steps_left)++;
            } while(*incremented_coord != initial_coord);
        }

        PatternData getPatternData(const Intersection &its) const {
            float u = fmod(its.uv.x*m_uscale,1.f);
            float v = fmod(its.uv.y*m_vscale,1.f);
            //TODO(Vidar): Check why this crashes sometimes
            if (u < 0.f) {
                u = u - floor(u);
            }
            if (v < 0.f) {
                v = v - floor(v);
            }
            uint32_t pattern_x = (uint32_t)(v*(float)(m_pattern_width));
            uint32_t pattern_y = (uint32_t)(u*(float)(m_pattern_height));

            AssertEx(pattern_x < m_pattern_width, "pattern_x larger than pwidth");
            AssertEx(pattern_y < m_pattern_height, "pattern_y larger than pheight");

            PaletteEntry current_point = m_pattern_entry[pattern_x +
                pattern_y*m_pattern_width];        

            //Calculate the size of the segment
            uint32_t steps_left_warp = 0, steps_right_warp = 0;
            uint32_t steps_left_weft = 0, steps_right_weft = 0;
            if (current_point.warp_above) {
                calculateLengthOfSegment(current_point.warp_above, pattern_x,
                        pattern_y, &steps_left_warp, &steps_right_warp);
            }else{
                calculateLengthOfSegment(current_point.warp_above, pattern_x,
                        pattern_y, &steps_left_weft, &steps_right_weft);
            }

            //Get the x y coordinates withing the thread segment
            float w = (steps_left_weft + steps_right_weft + 1.f);
            float x = ((v*(float)(m_pattern_width) - (float)pattern_x)
                    + steps_left_weft)/w;

            float h = (steps_left_warp + steps_right_warp + 1.f);
            float y = ((u*(float)(m_pattern_height) - (float)pattern_y)
                    + steps_left_warp)/h;

            //Rescale x and y to [-1,1]
            x = x*2.f - 1.f;
            y = y*2.f - 1.f;

            //Switch X and Y for weft, so that we always have the thread
            // cylinder going along the x axis
            if(current_point.warp_above){
                float tmp = x;
                x = y;
                y = tmp;
            }

            //TODO(Vidar): Use a parameter for choosing model?
            //Calculate the u and v coordinates along the curved cylinder
            //NOTE: This is different from how Irawan does it
            /* Our */
            //float segment_u = asinf(x*sinf(m_umax));
            //float segment_v = asinf(y);
            /* Irawan */
            float segment_u = x*m_umax;
            float segment_v = y*M_PI_2;

            //Calculate the normal in thread-local coordinates
            float normal[3] = {sinf(segment_u), sinf(segment_v)*cosf(segment_u),
                cosf(segment_v)*cosf(segment_u)};

            //Switch the x & y again to get back to uv space
            if(current_point.warp_above){
                float tmp = normal[0];
                normal[0] = normal[1];
                normal[1] = tmp;
            }

            //Get the world space coordinate vectors going along the texture u&v
            //axes
            Float dDispDu = normal[0];
            Float dDispDv = normal[1];
            Vector dpdu = its.dpdv + its.shFrame.n * (
                    -dDispDu - dot(its.shFrame.n, its.dpdu));
            Vector dpdv = its.dpdu + its.shFrame.n * (
                    -dDispDv - dot(its.shFrame.n, its.dpdv));

            //set frame
            Frame result;
            result.n = normalize(cross(dpdu, dpdv));

            result.s = normalize(dpdu - result.n
                    * dot(result.n, dpdu));
            result.t = cross(result.n, result.s);

            //Flip the normal if it points in the wrong direction
            if (dot(result.n, its.geoFrame.n) < 0)
                result.n *= -1;


            PatternData ret_data = {};
            ret_data.frame = result;
            ret_data.color.fromSRGB(current_point.color[0], current_point.color[1],
                    current_point.color[2]);

            ret_data.u = segment_u;
            ret_data.v = segment_v;
            ret_data.x = x; 
            ret_data.y = y; 
            ret_data.warp_above = current_point.warp_above; 

            //return the results
            return ret_data;
        }

        float specularReflectionPattern(Vector wi, Vector wo, PatternData data) const {
            //Fiber staple twist
            //u = segment_u
            float u = data.u;
            //float v = data.v;
            //float x = data.x;
            float y = data.y;
            
            // Half-vector, for some reason it seems to already be in the
            // correct coordinate frame... 
            Vector H = normalize(wi + wo);
            H.y *= -1.f; //TODO(Vidar): This is rather strange...
            if(data.warp_above){
                float tmp = H.x;
                H.x = H.y;
                H.y = tmp;
            }

            float staple_psi = M_PI_2;

            float D;
            {
                float a = H.y*sin(u) + H.z*cos(u);
                D = (H.y*cos(u)-H.z*sin(u))/(sqrt(H.x*H.x + a*a)) / tan(staple_psi);
            }

            float reflection = 0.f;

            float specular_v = atan2(-H.y*sin(u) - H.z*cos(u), H.x) + acos(D); //Plus eller minus i sista termen.
            //TODO(Vidar): Clamp specular_v, do we need it?
            if (fabsf(specular_v) < M_PI_2) {
                //we have specular reflection
                
                //get specular_y, using irawans transformation.
                float specular_y = specular_v/M_PI_2;

                // our transformation
                //float specular_y = sinf(specular_v);

                float deltaY = 0.4; // [0,0.1]
                if (fabsf(specular_y - y) < deltaY) {
                    reflection = 1.f;
                }
            }

            return reflection;
        }

        Spectrum eval(const BSDFSamplingRecord &bRec, EMeasure measure) const {
            if (!(bRec.typeMask & EDiffuseReflection) || measure != ESolidAngle
                    || Frame::cosTheta(bRec.wi) <= 0
                    || Frame::cosTheta(bRec.wo) <= 0)
                return Spectrum(0.0f);

            // Perturb the sampling record, in turn normal to match our thread normals
            PatternData pattern_data = getPatternData(bRec.its);
            Intersection perturbed(bRec.its);
            perturbed.shFrame = pattern_data.frame;

            float specularStrength = 0.2f;

            Vector perturbed_wo = perturbed.toLocal(bRec.its.toWorld(bRec.wo));
            //Return black if the perturbed direction lies below the surface
            if (Frame::cosTheta(bRec.wo) * Frame::cosTheta(perturbed_wo) <= 0)
                return Spectrum(0.0f);
            
            Spectrum specular(specularStrength*specularReflectionPattern(bRec.wi, bRec.wo, pattern_data));
            return pattern_data.color*(1.f - specularStrength) * (INV_PI * Frame::cosTheta(perturbed_wo)) + specular;
        }

        Float pdf(const BSDFSamplingRecord &bRec, EMeasure measure) const {
            if (!(bRec.typeMask & EDiffuseReflection) || measure != ESolidAngle
                    || Frame::cosTheta(bRec.wi) <= 0
                    || Frame::cosTheta(bRec.wo) <= 0)
                return 0.0f;

            const Intersection& its = bRec.its;
            PatternData pattern_data = getPatternData(its);
            Intersection perturbed(its);
            perturbed.shFrame = pattern_data.frame;

            return warp::squareToCosineHemispherePdf(perturbed.toLocal(
                        its.toWorld(bRec.wo)));

        }

        Spectrum sample(BSDFSamplingRecord &bRec, const Point2 &sample) const {
            if (!(bRec.typeMask & EDiffuseReflection) || Frame::cosTheta(bRec.wi) <= 0)
                return Spectrum(0.0f);
            const Intersection& its = bRec.its;
            PatternData pattern_data = getPatternData(its);
            Intersection perturbed(its);
            perturbed.shFrame = pattern_data.frame;
            bRec.wi = perturbed.toLocal(its.toWorld(bRec.wi));
            Vector perturbed_wo = warp::squareToCosineHemisphere(sample);
            if (!pattern_data.color.isZero()) {
                bRec.sampledComponent = 0;
                bRec.sampledType = EDiffuseReflection;
                bRec.wo = its.toLocal(perturbed.toWorld(perturbed_wo));
                bRec.eta = 1.f;
                if (Frame::cosTheta(perturbed_wo) * Frame::cosTheta(bRec.wo) <= 0)
                    return Spectrum(0.0f);
            }
            return pattern_data.color;
        }

        Spectrum sample(BSDFSamplingRecord &bRec, Float &pdf, const Point2 &sample) const {
            if (!(bRec.typeMask & EDiffuseReflection) || Frame::cosTheta(bRec.wi) <= 0)
                return Spectrum(0.0f);

            const Intersection& its = bRec.its;
            Intersection perturbed(its);
            PatternData pattern_data = getPatternData(its);
            perturbed.shFrame = pattern_data.frame;
            bRec.wi = perturbed.toLocal(its.toWorld(bRec.wi));

            Vector perturbed_wo = warp::squareToCosineHemisphere(sample);
            pdf = warp::squareToCosineHemispherePdf(perturbed_wo);

            if (!pattern_data.color.isZero()) {
                bRec.sampledComponent = 0;
                bRec.sampledType = EDiffuseReflection;
                bRec.wo = its.toLocal(perturbed.toWorld(
                    perturbed_wo));
                bRec.eta = 1.f;
                if (Frame::cosTheta(perturbed_wo) * Frame::cosTheta(bRec.wo) <= 0)
                    return Spectrum(0.0f);
            }
            return pattern_data.color;
        }

        void addChild(const std::string &name, ConfigurableObject *child) {
            BSDF::addChild(name, child);
        }

        void serialize(Stream *stream, InstanceManager *manager) const {
            BSDF::serialize(stream, manager);
            //TODO(Vidar): Serialize our parameters
        }

        Float getRoughness(const Intersection &its, int component) const {
            return std::numeric_limits<Float>::infinity();
        }

        std::string toString() const {
            //TODO(Vidar): Add our parameters here...
            std::ostringstream oss;
            oss << "Cloth[" << endl
                << "  id = \"" << getID() << "\"," << endl
                << "]";
            return oss.str();
        }

        Shader *createShader(Renderer *renderer) const;

        MTS_DECLARE_CLASS()
    private:
            ref<Texture> m_reflectance;
};

// ================ Hardware shader implementation ================

class SmoothDiffuseShader : public Shader {
    public:
        SmoothDiffuseShader(Renderer *renderer, const Texture *reflectance)
            : Shader(renderer, EBSDFShader), m_reflectance(reflectance) {
                m_reflectanceShader = renderer->registerShaderForResource(m_reflectance.get());
            }

        bool isComplete() const {
            return m_reflectanceShader.get() != NULL;
        }

        void cleanup(Renderer *renderer) {
            renderer->unregisterShaderForResource(m_reflectance.get());
        }

        void putDependencies(std::vector<Shader *> &deps) {
            deps.push_back(m_reflectanceShader.get());
        }

        void generateCode(std::ostringstream &oss,
                const std::string &evalName,
                const std::vector<std::string> &depNames) const {
            oss << "vec3 " << evalName << "(vec2 uv, vec3 wi, vec3 wo) {" << endl
                << "    if (cosTheta(wi) < 0.0 || cosTheta(wo) < 0.0)" << endl
                << "    	return vec3(0.0);" << endl
                << "    return " << depNames[0] << "(uv) * inv_pi * cosTheta(wo);" << endl
                << "}" << endl
                << endl
                << "vec3 " << evalName << "_diffuse(vec2 uv, vec3 wi, vec3 wo) {" << endl
                << "    return " << evalName << "(uv, wi, wo);" << endl
                << "}" << endl;
        }

        MTS_DECLARE_CLASS()
    private:
            ref<const Texture> m_reflectance;
            ref<Shader> m_reflectanceShader;
};

Shader *Cloth::createShader(Renderer *renderer) const {
    return new SmoothDiffuseShader(renderer, m_reflectance.get());
}

    MTS_IMPLEMENT_CLASS(SmoothDiffuseShader, false, Shader)
MTS_IMPLEMENT_CLASS_S(Cloth, false, BSDF)
    MTS_EXPORT_PLUGIN(Cloth, "Smooth diffuse BRDF")
    MTS_NAMESPACE_END