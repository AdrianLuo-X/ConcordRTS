int runtimeParasiteRuleFramesToTicks(int frames, int fixedTickHz) {
    return std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(std::max(0, frames))
                * static_cast<double>(std::max(1, fixedTickHz))
                / 15.0
        ))
    );
}
