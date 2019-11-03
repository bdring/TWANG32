#include "Arduino.h"

class Lava
{
  public:
    void Spawn(int left, int right, int ontime, int offtime, int offset, int state, float grow_rate, float flow_vector);
    void Kill();
    int Alive();
	void Update();
    int _left;
    int _right;
    int _ontime;
    int _offtime;
    int _offset;
    long _lastOn;
    int _state;
	float _grow_rate = 0.0; // size grows by this much each tick
	float _flow_vector = 0.0; // endpoints move in the direction each tick.
    static const int OFF = 0;
    static const int ON = 1;
  private:
    int _alive;
	float _growth = 0;
	float _flow = 0;
	int _width;
};

void Lava::Spawn(int left, int right, int ontime, int offtime, int offset, int state, float grow_rate, float flow_vector){
    _left = left;
    _right = right;
    _ontime = ontime;
    _offtime = offtime;
    _offset = offset;
    _alive = 1;
    _lastOn = millis()-offset;
    _state = state;
	
	_width = _right - _left;
	
	_grow_rate = fabs(grow_rate); // only allow positive growth
	_flow_vector = flow_vector;
	
	
}

void Lava::Kill(){
    _alive = 0;
}

int Lava::Alive(){
    return _alive;
}

// this gets called on every frame.
void Lava::Update() {
	// update how much it has changed	
	if (_grow_rate != 0) {
		_growth += _grow_rate;
		if (_growth >= 1.0) {
			if (_left > 0)
				_left -= 1;
			
			if (_right < VIRTUAL_LED_COUNT)
				_right += 1;
			
			_growth = 0.0;
		}
	}
	
	if (_flow_vector != 0) {
		_flow += _flow_vector;
		if (fabs(_flow) >=1) {
			if (_left > 1 && _left < VIRTUAL_LED_COUNT - _width) {
				_left += (int)_flow;				
			}
			if (_right > _width && _right < VIRTUAL_LED_COUNT)
				_right += (int)_flow;
			
				
			_flow = 0.0;
		}
	}
		
}
