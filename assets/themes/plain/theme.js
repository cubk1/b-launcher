function colors(env) {
    var a = env.accent;
    return {
        accentSoft: color.mix(color.parse("#1E1E1E"), a, 0.2),
        accentText: color.add(a, 30),
        selection: color.alpha(a, 110),
        logSelection: color.alpha(a, 102),
        onAccent: color.ink(a),
        accent2: a
    };
}
