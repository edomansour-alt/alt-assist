#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>
#include <algorithm>
#include <random>

using namespace geode::prelude;

namespace assist {
	struct State {
		double now = 0;               // game-time clock (sum of physics dt)
		std::deque<double> presses;   // timestamps of REAL presses
		double cps = 0;               // smoothed real CPS
		double lastRealEvent = -100;
		double lastToggle = 0;
		bool realDown = false;        // what your finger is doing
		bool outDown = false;         // what the game is being told
		bool active = false;          // assist currently driving input
		bool injecting = false;       // true while WE send a click
		int mode = 0;                 // 0 none, 1 ship, 2 wave
		double targetCps = 0;
	};
	inline State S;

	inline double getD(char const* k) { return Mod::get()->getSettingValue<double>(k); }
	inline bool getB(char const* k) { return Mod::get()->getSettingValue<bool>(k); }

	inline double jitter() {
		static std::mt19937 rng{std::random_device{}()};
		std::uniform_real_distribution<double> d(-1.0, 1.0);
		return d(rng);
	}

	inline int currentMode(GJBaseGameLayer* l) {
		auto p = l->m_player1;
		if (!p) return 0;
		if (p->m_isDart) return 2;
		if (p->m_isShip) return 1;
		return 0;
	}

	inline bool allowed(GJBaseGameLayer* l) {
		if (!getB("enabled") || l->m_isPlatformer) return false;
		if (getB("practice-only")) {
			if (auto pl = PlayLayer::get()) {
				return pl->m_isPracticeMode || pl->m_isTestMode;
			}
			return true; // editor playtest
		}
		return true;
	}

	inline double threshold(int mode) {
		return mode == 2 ? getD("wave-threshold") : getD("ship-threshold");
	}

	inline void recordPress() {
		S.presses.push_back(S.now);
		while (!S.presses.empty() && S.presses.front() < S.now - 0.6) S.presses.pop_front();
		while (S.presses.size() > 12) S.presses.pop_front();
		if (S.presses.size() >= 3) {
			double span = S.presses.back() - S.presses.front();
			if (span > 0.0001) {
				double raw = (S.presses.size() - 1) / span;
				S.cps = S.cps > 0 ? S.cps * 0.5 + raw * 0.5 : raw;
			}
		}
	}

	inline void sendOut(GJBaseGameLayer* l, bool down) {
		S.outDown = down;
		S.injecting = true;
		l->GJBaseGameLayer::handleButton(down, 1, true);
		S.injecting = false;
	}

	// runs once per physics step, before the game processes input
	inline void tick(GJBaseGameLayer* l, float dt, bool halfTick) {
		if (!halfTick) S.now += dt;
		int mode = currentMode(l);

		if (!allowed(l)) {
			if (S.active) { S.active = false; if (S.outDown != S.realDown) sendOut(l, S.realDown); }
			return;
		}

		double thr = threshold(mode);
		double timeout = std::max(0.2, 1.6 / std::max(S.cps, 1.0));
		bool stale = (S.now - S.lastRealEvent) > timeout;

		if (stale) { S.cps = 0; S.presses.clear(); }

		if (!S.active) {
			if (mode != 0 && !stale && S.cps >= thr) {
				S.active = true;
				S.mode = mode;
				S.lastToggle = S.now;
			}
		} else if (mode != S.mode || stale || S.cps < thr * 0.8) {
			// hand control back to the player
			S.active = false;
			if (S.outDown != S.realDown) sendOut(l, S.realDown);
			return;
		}

		if (!S.active) return;

		double tc = getB("matched-cps") ? S.cps * getD("speed-boost") : thr * getD("speed-boost");
		tc = std::clamp(tc, thr, getD("max-cps"));
		S.targetCps = tc;

		double half = 0.5 / tc;
		half *= 1.0 + jitter() * (getD("humanize") / 100.0);

		if (S.now - S.lastToggle >= half) {
			// don't try to catch up on a big backlog, just resync
			S.lastToggle = (S.now - S.lastToggle > half * 3) ? S.now : S.lastToggle + half;
			sendOut(l, !S.outDown);
		}
	}
}

class $modify(AssistLayer, GJBaseGameLayer) {
	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		assist::tick(this, dt, isHalfTick);
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		using namespace assist;
		if (S.injecting || button != 1 || !isPlayer1 || m_isPlatformer) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		// real player input
		S.realDown = down;
		S.lastRealEvent = S.now;
		if (down) recordPress();

		// while assisting, the scheduler owns the output; real clicks only feed the CPS meter
		if (S.active && allowed(this)) return;

		S.outDown = down;
		S.lastToggle = S.now;
		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}
};

class $modify(AssistPlayLayer, PlayLayer) {
	struct Fields {
		CCLabelBMFont* label = nullptr;
	};

	void setupHasCompleted() {
		PlayLayer::setupHasCompleted();
		assist::S = assist::State();

		auto label = CCLabelBMFont::create("", "chatFont.fnt");
		label->setScale(0.5f);
		label->setAnchorPoint({0.f, 1.f});
		label->setPosition({6.f, CCDirector::get()->getWinSize().height - 4.f});
		label->setOpacity(150);
		label->setZOrder(1000);
		this->addChild(label);
		m_fields->label = label;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		assist::S = assist::State();
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		auto label = m_fields->label;
		if (!label) return;
		if (!assist::getB("show-label") || !assist::getB("enabled")) {
			label->setVisible(false);
			return;
		}
		label->setVisible(true);
		if (assist::S.active) {
			label->setString(fmt::format("SPAM ASSIST {:.1f} CPS | {}",
				assist::S.targetCps, assist::S.mode == 2 ? "WAVE" : "SHIP").c_str());
		} else {
			label->setString(fmt::format("SPAM ASSIST idle ({:.0f} CPS)", assist::S.cps).c_str());
		}
	}
};
