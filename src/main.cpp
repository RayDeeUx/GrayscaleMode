#include <ninxout.options_api/include/API.hpp>
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

CCTexture2D* capture(CCRenderTexture* renderTexture, PlayLayer* pl) { // taken from death animations so any bugs are also there
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

	if (!renderTexture) renderTexture = CCRenderTexture::create(winSize.width, winSize.height);

	renderTexture->beginWithClear(0, 0, 0, 1);
	pl->visit();
	renderTexture->end();

	director->m_obWinSizeInPoints = ogRes;
	view->setDesignResolutionSize(ogRes.width, ogRes.height, ResolutionPolicy::kResolutionExactFit);
	view->m_fScaleX = ogScale.x;
	view->m_fScaleY = ogScale.y;

	return renderTexture->getSprite()->getTexture();
}

class $modify(MyPlayLayer, PlayLayer) {
	struct Fields {
		Ref<CCRenderTexture> m_renderTexture = nullptr;
		CCSprite* m_frame = nullptr;
	};
	
	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		
		auto f = m_fields.self();
		if (!f->m_renderTexture || !f->m_frame) return;
		
		f->m_frame->setVisible(false);
		capture(f->m_renderTexture, this);
		f->m_frame->setVisible(true);
	}
	
	void setupHasCompleted() {
		PlayLayer::setupHasCompleted();

		auto f = m_fields.self();
		if (f->m_frame) return;

		CCSize size = CCDirector::get()->getWinSize();
		f->m_renderTexture = CCRenderTexture::create(size.width, size.height);

		f->m_frame = CCSprite::createWithTexture(capture(f->m_renderTexture, this));
		f->m_frame->setAnchorPoint({0, 0});
		f->m_frame->setFlipY(true);
		f->m_frame->setBlendFunc(ccBlendFunc{GL_ONE, GL_ZERO});
		f->m_frame->setID("grayscale-mode-sprite"_spr);

		addChild(f->m_frame);

		int highest = reinterpret_cast<CCScene*>(this)->getHighestChildZ();
		if (highest < std::numeric_limits<int>::max()) highest++;
		f->m_frame->setZOrder(highest);

		CCShaderCache* cache = CCShaderCache::sharedShaderCache();
		CCGLProgram* shader = cache->programForKey("grayscale-shader"_spr);

		if (!shader) {
			shader = new CCGLProgram();
			shader->initWithVertexShaderByteArray(vert.c_str(), grayscale.c_str());
			shader->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
			shader->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
			shader->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
			shader->link();
			shader->updateUniforms();
			shader->retain();

			cache->addProgram(shader, "grayscale-shader"_spr);
		}

		shader->use();
		shader->setUniformLocationWith1f(glGetUniformLocation(shader->getProgram(), "u_intensity"), std::clamp<float>(static_cast<float>(Mod::get()->getSettingValue<double>("intensity")), 0.f, 1.f));

		f->m_frame->setShaderProgram(shader);
		if (!Mod::get()->getSettingValue<bool>("enabled")) f->m_frame->setOpacity(0);
	}
};

#define ADD_LEVELINFOLAYER_TOGGLE(displayName, settingsID, detailedDesc)\
	OptionsAPI::addPreLevelSetting<bool>(\
		displayName,\
		settingsID""_spr,\
		[](GJGameLevel*) {\
			const bool origValue = Mod::get()->getSettingValue<bool>(settingsID);\
			Mod::get()->setSettingValue<bool>(settingsID, !origValue);\
		},\
		[](GJGameLevel*) {\
			return Mod::get()->getSettingValue<bool>(settingsID);\
		},\
		"<cl>(From GrayscaleMode)</c>\n" detailedDesc\
	);

#define ADD_PAUSELAYER_TOGGLE(displayName, settingsID, detailedDesc)\
	OptionsAPI::addMidLevelSetting<bool>(\
		displayName,\
		settingsID""_spr,\
		[](GJBaseGameLayer* gjbgl) {\
			const bool origValue = Mod::get()->getSettingValue<bool>(settingsID);\
			Mod::get()->setSettingValue<bool>(settingsID, !origValue);\
			PlayLayer* pl = PlayLayer::get();\
			if (gjbgl != pl) return;\
			CCNode* grayscaleModeSprite = pl->getChildByID("grayscale-mode-sprite"_spr);\
			if (!grayscaleModeSprite) return;\
			GLubyte determinedOpacity = origValue ? 0 : 255;\
			static_cast<CCSprite*>(grayscaleModeSprite)->setOpacity(determinedOpacity);\
		},\
		[](GJBaseGameLayer* gjbgl) {\
			return Mod::get()->getSettingValue<bool>(settingsID);\
		},\
		"<cl>(From GrayscaleMode)</c>\n" detailedDesc\
	);

$on_mod(Loaded) {
	ADD_LEVELINFOLAYER_TOGGLE("Grayscale Mode", "enabled", "Play the level with Grayscale Mode enabled.")
	ADD_PAUSELAYER_TOGGLE("Grayscale Mode", "enabled", "Play the level with Grayscale Mode enabled.")
}

#ifndef GEODE_IS_IOS
#include <geode.custom-keybinds/include/OptionalAPI.hpp>
using namespace keybinds;
$on_mod(Loaded) {
	(void)[&]() -> Result<> {
		GEODE_UNWRAP(BindManagerV2::registerBindable(GEODE_UNWRAP(BindableActionV2::create(
			"toggle-grayscale"_spr,
			"Toggle Grayscale Mode",
			"Toggles Grayscale Mode when playing a level.",
			{ GEODE_UNWRAP(KeybindV2::create(KEY_X, ModifierV2::Shift)) },
			GEODE_UNWRAP(CategoryV2::create("Play/GrayscaleMode"))
		))));
		return Ok();
	}();
	(void)[&]() -> Result<> {
		GEODE_UNWRAP(BindManagerV2::registerBindable(GEODE_UNWRAP(BindableActionV2::create(
			"raise-intensity"_spr,
			"Raise Intensity",
			"Increase the intensity of Grayscale Mode by 0.01.",
			{ GEODE_UNWRAP(KeybindV2::create(KEY_C, ModifierV2::Shift)) },
			GEODE_UNWRAP(CategoryV2::create("Play/GrayscaleMode"))
		))));
		return Ok();
	}();
	(void)[&]() -> Result<> {
		GEODE_UNWRAP(BindManagerV2::registerBindable(GEODE_UNWRAP(BindableActionV2::create(
			"lower-intensity"_spr,
			"Lower Intensity",
			"Decrease the intensity of Grayscale Mode by 0.01.",
			{ GEODE_UNWRAP(KeybindV2::create(KEY_Z, ModifierV2::Shift)) },
			GEODE_UNWRAP(CategoryV2::create("Play/GrayscaleMode"))
		))));
		return Ok();
	}();
	/*
	const bool origValue = Mod::get()->getSettingValue<bool>(settingsID);\
	Mod::get()->setSettingValue<bool>(settingsID, !origValue);\
	PlayLayer* pl = PlayLayer::get();\
	if (gjbgl != pl) return;\
	CCNode* grayscaleModeSprite = pl->getChildByID("grayscale-mode-sprite"_spr);\
	if (!grayscaleModeSprite) return;\
	GLubyte determinedOpacity = origValue ? 0 : 255;\
	static_cast<CCSprite*>(grayscaleModeSprite)->setOpacity(determinedOpacity);\
	*/
	new EventListener([=](InvokeBindEventV2* event) {
		if (!event->isDown()) return ListenerResult::Propagate;

		const bool origValue = Mod::get()->getSettingValue<bool>("enabled");
		Mod::get()->setSettingValue<bool>("enabled", !origValue);

		PlayLayer* pl = PlayLayer::get();
		if (!pl) return ListenerResult::Propagate;

		CCNode* grayscaleModeSprite = pl->getChildByID("grayscale-mode-sprite"_spr);
		if (!grayscaleModeSprite) return ListenerResult::Propagate;

		GLubyte determinedOpacity = origValue ? 0 : 255;
		static_cast<CCSprite*>(grayscaleModeSprite)->setOpacity(determinedOpacity);
	return ListenerResult::Propagate;
	}, InvokeBindFilterV2(nullptr, "toggle-grayscale"_spr));
	new EventListener([=](InvokeBindEventV2* event) {
		if (!event->isDown()) return ListenerResult::Propagate;

		const double origValue = Mod::get()->getSettingValue<double>("intensity");
		const float newValue = std::clamp<float>(static_cast<float>(origValue + .01f), 0.f, 1.f);
		Mod::get()->setSettingValue<double>("intensity", newValue);

		PlayLayer* pl = PlayLayer::get();
		if (!pl) return ListenerResult::Propagate;

		CCNode* grayscaleModeSprite = pl->getChildByID("grayscale-mode-sprite"_spr);
		if (!grayscaleModeSprite) return ListenerResult::Propagate;

		CCShaderCache* cache = CCShaderCache::sharedShaderCache();
		CCGLProgram* shader = cache->programForKey("grayscale-shader"_spr);
		if (!shader) return ListenerResult::Propagate;

		shader->use();
		shader->setUniformLocationWith1f(glGetUniformLocation(shader->getProgram(), "u_intensity"), newValue);
	return ListenerResult::Propagate;
	}, InvokeBindFilterV2(nullptr, "raise-intensity"_spr));
	new EventListener([=](InvokeBindEventV2* event) {
		if (!event->isDown()) return ListenerResult::Propagate;

		const double origValue = Mod::get()->getSettingValue<double>("intensity");
		const float newValue = std::clamp<float>(static_cast<float>(origValue - .01f), 0.f, 1.f);
		Mod::get()->setSettingValue<double>("intensity", newValue);

		PlayLayer* pl = PlayLayer::get();
		if (!pl) return ListenerResult::Propagate;

		CCNode* grayscaleModeSprite = pl->getChildByID("grayscale-mode-sprite"_spr);
		if (!grayscaleModeSprite) return ListenerResult::Propagate;

		CCShaderCache* cache = CCShaderCache::sharedShaderCache();
		CCGLProgram* shader = cache->programForKey("grayscale-shader"_spr);
		if (!shader) return ListenerResult::Propagate;

		shader->use();
		shader->setUniformLocationWith1f(glGetUniformLocation(shader->getProgram(), "u_intensity"), newValue);
	return ListenerResult::Propagate;
	}, InvokeBindFilterV2(nullptr, "lower-intensity"_spr));
}
#endif