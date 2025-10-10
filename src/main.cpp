#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

inline const std::string vert = R"(
    attribute vec4 a_position;
    attribute vec2 a_texCoord;
    attribute vec4 a_color;
    
    #ifdef GL_ES
    varying lowp vec4 v_fragmentColor;
    varying mediump vec2 v_texCoord;
    #else
    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    #endif
    
    void main() {
        gl_Position = CC_MVPMatrix * a_position;
        v_fragmentColor = a_color;
        v_texCoord = a_texCoord;
    }
)";

inline const std::string grayscale = R"(
    #ifdef GL_ES
    precision mediump float;
    #endif
    
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform float u_intensity;
    
    void main() {
        vec4 color = texture2D(u_texture, v_texCoord);
        float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
        vec3 mixed = mix(color.rgb, vec3(gray), clamp(u_intensity, 0.0, 1.0));
        gl_FragColor = vec4(mixed, color.a);
    }

)";

CCTexture2D* capture(CCRenderTexture* renderTexture) { // taken from death animations so any bugs are also there
    CCEGLView* view = CCEGLView::get();
    CCDirector* director = CCDirector::get();

    CCSize winSize = director->getWinSize();
    CCSize ogRes = view->getDesignResolutionSize();
    CCSize size = { roundf(320.f * (winSize.width / winSize.height)), 320.f };
    CCSize newScale = { winSize.width / size.width, winSize.height / size.height };
    CCPoint ogScale = { view->m_fScaleX, view->m_fScaleY };

    float scale = director->getContentScaleFactor() / utils::getDisplayFactor();

    director->m_obWinSizeInPoints = size;
    view->setDesignResolutionSize(size.width, size.height, ResolutionPolicy::kResolutionExactFit);
    view->m_fScaleX = scale * newScale.width;
    view->m_fScaleY = scale * newScale.height;

    if (!renderTexture)
        renderTexture = CCRenderTexture::create(winSize.width, winSize.height);

    renderTexture->beginWithClear(0, 0, 0, 1);
    CCScene::get()->visit();
    renderTexture->end();

    director->m_obWinSizeInPoints = ogRes;
    view->setDesignResolutionSize(ogRes.width, ogRes.height, ResolutionPolicy::kResolutionExactFit);
    view->m_fScaleX = ogScale.x;
    view->m_fScaleY = ogScale.y;

    return renderTexture->getSprite()->getTexture();
}

class $modify(PlayLayer) {
    
    struct Fields {
        
        Ref<CCRenderTexture> m_renderTexture = nullptr;
        
        CCSprite* m_frame = nullptr;
    };
    
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        
        auto f = m_fields.self();
        
        if (!f->m_renderTexture || !f->m_frame) return;
        
        f->m_frame->setVisible(false);
        
        capture(f->m_renderTexture);
        
        f->m_frame->setVisible(true);
    }
    
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        
        if (
            Mod::get()->getSettingValue<float>("intensity") <= 0.f
            || Mod::get()->getSettingValue<bool>("disable")
        ) {
            return;
        }
        
        auto f = m_fields.self();
        
        if (f->m_frame) return;
        
        CCSize size = CCDirector::get()->getWinSize();
        
        f->m_renderTexture = CCRenderTexture::create(size.width, size.height);
        
        f->m_frame = CCSprite::createWithTexture(capture(f->m_renderTexture));
        f->m_frame->setAnchorPoint({0, 0});
        f->m_frame->setFlipY(true);
        f->m_frame->setBlendFunc(ccBlendFunc{GL_ONE, GL_ZERO});
        
        addChild(f->m_frame);
        
        int highest = reinterpret_cast<CCScene*>(this)->getHighestChildZ();
    
        if (highest < std::numeric_limits<int>::max())
            highest++;
    
        f->m_frame->setZOrder(highest);
        
        CCShaderCache* cache = CCShaderCache::sharedShaderCache();
        CCGLProgram* shader = cache->programForKey("shader"_spr);
        
        if (!shader) {
            shader = new CCGLProgram();
            shader->initWithVertexShaderByteArray(vert.c_str(), grayscale.c_str());
            shader->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
            shader->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
            shader->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
            shader->link();
            shader->updateUniforms();
            shader->retain();
            
            cache->addProgram(shader, "shader"_spr);
        }
        
        shader->use();
        shader->setUniformLocationWith1f(
            glGetUniformLocation(shader->getProgram(), "u_intensity"),
            Mod::get()->getSettingValue<float>("intensity")
        );
        
        f->m_frame->setShaderProgram(shader);
    }
    
};