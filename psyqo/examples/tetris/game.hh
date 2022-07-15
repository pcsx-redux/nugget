/*

MIT License

Copyright (c) 2022 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <stdint.h>

#include "psyqo/scene.hh"
#include "psyqo/simplepad.hh"

class MainGame final : public psyqo::Scene {
  public:
    void render();

  private:
    void start(Scene::StartReason reason) override;
    void frame() override;
    void teardown(Scene::TearDownReason reason) override;

    void tick();
    void buttonEvent(const psyqo::SimplePad::Event& event);

    void createBlock();
    void moveLeft();
    void moveRight();
    void rotateLeft();
    void rotateRight();
    void rotate(unsigned rotation);
    void recomputePeriod();

    unsigned m_timer;
    unsigned m_score;
    uint32_t m_period;
    uint32_t m_fastPeriod;
    uint8_t m_currentBlock, m_blockRotation;
    int8_t m_blockX, m_blockY;
    bool m_gameOver = false;
    bool m_paused = false;
    bool m_bottomHitOnce = false;
    bool m_needsToUpdateFieldFragment = false;
    bool m_needsToUpdateBlockFragment = false;
};
extern MainGame g_mainGame;