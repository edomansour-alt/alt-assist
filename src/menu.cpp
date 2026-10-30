#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

class AssistMenu;

namespace {
	struct ToggleDef { char const* id; char const* name; };
	constexpr ToggleDef kToggles[] = {
		{"enabled",       "Assist enabled"},
		{"practice-only", "Practice / test only"},
		{"matched-cps",   "Matched CPS"},
		{"show-label",    "Show CPS label"},
	};

	struct StepDef { char const* id; char const* name; double step, lo, hi; int decimals; };
	constexpr StepDef kSteps[] = {
		{"ship-threshold", "Ship starts at (CPS)", 1.0,  2.0, 30.0, 0},
		{"wave-threshold", "Wave starts at (CPS)", 1.0,  2.0, 30.0, 0},
		{"max-cps",        "Max CPS",              1.0,  6.0, 30.0, 0},
		{"speed-boost",    "Speed multiplier",     0.05, 0.8, 1.5,  2},
		{"humanize",       "Humanization %",       1.0,  0.0, 15.0, 0},
	};

	AssistMenu* g_menu = nullptr;
	bool g_capturing = false;

	int menuKey() {
		return Mod::get()->getSavedValue<int>("menu-key", static_cast<int>(KEY_F2));
	}
}

class AssistMenu : public geode::Popup {
protected:
	CCLabelBMFont* m_values[std::size(kSteps)] = {};
	ButtonSprite* m_keySpr = nullptr;

	static std::string fmtValue(StepDef const& d, double v) {
		return fmt::format("{:.{}f}", v, d.decimals);
	}

	bool setupMenu() {
		g_menu = this;
		this->setTitle("Spam Assist");

		auto size = m_mainLayer->getContentSize();
		float W = size.width, H = size.height;
		float y = H - 52.f;
		float const row = 24.f;

		// toggles
		for (int i = 0; i < (int)std::size(kToggles); i++, y -= row) {
			auto lbl = CCLabelBMFont::create(kToggles[i].name, "bigFont.fnt");
			lbl->setScale(0.4f);
			lbl->setAnchorPoint({0.f, 0.5f});
			lbl->setPosition({18.f, y});
			m_mainLayer->addChild(lbl);

			auto tog = CCMenuItemToggler::createWithStandardSprites(
				this, menu_selector(AssistMenu::onToggle), 0.6f);
			tog->setTag(i);
			tog->toggle(Mod::get()->getSettingValue<bool>(kToggles[i].id));
			tog->setPosition({W - 32.f, y});
			m_buttonMenu->addChild(tog);
		}

		// steppers
		for (int i = 0; i < (int)std::size(kSteps); i++, y -= row) {
			auto const& d = kSteps[i];
			auto lbl = CCLabelBMFont::create(d.name, "bigFont.fnt");
			lbl->setScale(0.4f);
			lbl->setAnchorPoint({0.f, 0.5f});
			lbl->setPosition({18.f, y});
			m_mainLayer->addChild(lbl);

			auto minus = CCMenuItemSpriteExtra::create(
				ButtonSprite::create("-", "bigFont.fnt", "GJ_button_04.png", 0.6f),
				this, menu_selector(AssistMenu::onMinus));
			minus->setTag(i);
			minus->setPosition({W - 98.f, y});
			m_buttonMenu->addChild(minus);

			auto val = CCLabelBMFont::create(
				fmtValue(d, Mod::get()->getSettingValue<double>(d.id)).c_str(), "bigFont.fnt");
			val->setScale(0.4f);
			val->setPosition({W - 62.f, y});
			m_mainLayer->addChild(val);
			m_values[i] = val;

			auto plus = CCMenuItemSpriteExtra::create(
				ButtonSprite::create("+", "bigFont.fnt", "GJ_button_04.png", 0.6f),
				this, menu_selector(AssistMenu::onPlus));
			plus->setTag(i);
			plus->setPosition({W - 26.f, y});
			m_buttonMenu->addChild(plus);
		}

		// keybind button
		y -= 6.f;
		m_keySpr = ButtonSprite::create("", "bigFont.fnt", "GJ_button_01.png", 0.5f);
		auto keyBtn = CCMenuItemSpriteExtra::create(
			m_keySpr, this, menu_selector(AssistMenu::onBindKey));
		keyBtn->setPosition({W / 2.f, y});
		m_buttonMenu->addChild(keyBtn);
		refreshKeyLabel();

		return true;
	}

	void onToggle(CCObject* s) {
		auto t = static_cast<CCMenuItemToggler*>(s);
		// callback fires before the visual flip, so the new value is the opposite
		Mod::get()->setSettingValue<bool>(kToggles[t->getTag()].id, !t->isToggled());
	}

	void bump(CCObject* s, int dir) {
		int i = static_cast<CCNode*>(s)->getTag();
		auto const& d = kSteps[i];
		double v = Mod::get()->getSettingValue<double>(d.id) + dir * d.step;
		v = std::clamp(v, d.lo, d.hi);
		v = std::round(v * 100.0) / 100.0;
		Mod::get()->setSettingValue<double>(d.id, v);
		m_values[i]->setString(fmtValue(d, v).c_str());
	}
	void onMinus(CCObject* s) { bump(s, -1); }
	void onPlus(CCObject* s) { bump(s, +1); }

	void onBindKey(CCObject*) {
		g_capturing = true;
		refreshKeyLabel();
	}

public:
	void refreshKeyLabel() {
		if (!m_keySpr) return;
		if (g_capturing) {
			m_keySpr->setString("Press a key... (Esc = cancel)");
		} else {
			auto name = CCKeyboardDispatcher::get()->keyToString(static_cast<enumKeyCodes>(menuKey()));
			m_keySpr->setString(fmt::format("Menu key: {}", name ? name : "?").c_str());
		}
	}

	void closeMenu() { this->onClose(nullptr); }

	static AssistMenu* create() {
		auto ret = new AssistMenu();
		if (ret->Popup::init(330.f, 310.f) && ret->setupMenu()) {
			ret->autorelease();
			return ret;
		}
		CC_SAFE_DELETE(ret);
		return nullptr;
	}

	~AssistMenu() override {
		if (g_menu == this) g_menu = nullptr;
		g_capturing = false;
	}
};

namespace {
	void toggleMenu() {
		if (g_menu) { g_menu->closeMenu(); return; }

		// pause the run first so the game doesn't keep playing behind the menu
		if (auto pl = PlayLayer::get()) {
			if (!CCScene::get()->getChildByType<PauseLayer>(0)) {
				pl->pauseGame(false);
			}
		}
		if (auto m = AssistMenu::create()) m->show();
	}
}

// open with the bound key (keyboard)
class $modify(AssistKeys, CCKeyboardDispatcher) {
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
		if (down && !repeat) {
			if (g_capturing) {
				g_capturing = false;
				if (key != KEY_Escape) {
					Mod::get()->setSavedValue<int>("menu-key", static_cast<int>(key));
				}
				if (g_menu) g_menu->refreshKeyLabel();
				return true;
			}
			if (static_cast<int>(key) == menuKey()) {
				toggleMenu();
				return true;
			}
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
	}
};

// open from the pause menu (works on mobile / no keyboard)
class $modify(AssistPause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto menu = CCMenu::create();
		menu->setPosition({0.f, 0.f});
		auto btn = CCMenuItemSpriteExtra::create(
			ButtonSprite::create("Assist", "bigFont.fnt", "GJ_button_04.png", 0.5f),
			this, menu_selector(AssistPause::onAssist));
		btn->setPosition({50.f, 24.f});
		menu->addChild(btn);
		this->addChild(menu);
	}

	void onAssist(CCObject*) {
		if (auto m = AssistMenu::create()) m->show();
	}
};
