"""
Synthesize the menu's sound effects: short, soft sine tones with a quick attack and a gentle decay.

    python -I make_sounds.py <output folder>     (writes cursorsound, enter, back, settings, error .wav)

Mono, 44.1 kHz, 16-bit, like the stock sounds they replace (assets/sounds/).
"""
import math, os, struct, sys, wave

RATE = 44100


def tone(notes, peak):
    """notes: list of (frequency Hz, duration s, decay time constant s, overtone level)."""
    samples = []
    for freq, duration, decay, overtone in notes:
        n = int(RATE * duration)
        attack = int(RATE * 0.004)
        for i in range(n):
            t = i / RATE
            env = min(1.0, i / attack) * math.exp(-t / decay)
            # Fade the last few milliseconds so notes never click.
            tail = n - i
            if tail < RATE * 0.006:
                env *= tail / (RATE * 0.006)
            v = math.sin(2 * math.pi * freq * t) + overtone * math.sin(4 * math.pi * freq * t)
            samples.append(v * env / (1 + overtone))
    return [int(max(-1.0, min(1.0, s)) * peak) for s in samples]


SOUNDS = {
    # A soft tick for moving the cursor.
    'cursorsound': tone([(1568, 0.035, 0.010, 0.15)], 4200),
    # Rising two notes for choosing something.
    'enter': tone([(784, 0.050, 0.030, 0.2), (1175, 0.090, 0.040, 0.2)], 5200),
    # Falling two notes for going back.
    'back': tone([(988, 0.045, 0.028, 0.2), (659, 0.080, 0.035, 0.2)], 5000),
    # A light chime for menus and settings.
    'settings': tone([(1319, 0.140, 0.050, 0.35)], 4400),
    # Two low, rounded pulses for an error.
    'error': tone([(247, 0.080, 0.050, 0.5), (233, 0.120, 0.060, 0.5)], 6500),
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    for name, data in SOUNDS.items():
        path = os.path.join(out, name + '.wav')
        with wave.open(path, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(struct.pack('<%dh' % len(data), *data))
        print('%-12s %.3f s' % (name, len(data) / RATE))


if __name__ == '__main__':
    main()
