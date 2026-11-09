function colors(env) {
    var a = env.accent;
    return {
        accentSoft: color.mix(color.parse("#FFFFFF"), a, 0.14),
        accentText: color.scale(a, 0.72),
        selection: color.alpha(a, 90),
        logSelection: color.alpha(a, 71),
        onAccent: color.ink(a),
        accent2: a
    };
}
