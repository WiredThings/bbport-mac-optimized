"""Keep the manual QA entry on original physical input, separate from automation."""
FORBIDDEN = frozenset({
    'BB_ARM_QA_PAD_FILE', 'BB_PAD_FILE', 'BB_PAD_FILE_ONLY',
    'BB_BENCHMARK_MUTE', 'BB_FRAME_LOG_ONLY', 'BB_FRAME_TIMING',
    'BB_STUTTER_AUDIT', 'BB_STUTTER_GATE', 'BB_CAPTURE_NATIVE',
    'BB_CAPTURE_TEXTURE_BINDINGS', 'BB_CAPTURE_TRIGGER',
})

def validate_manual_environment(environment):
    leaked = FORBIDDEN.intersection(environment)
    if leaked:
        raise ValueError('manual QA contains automation-only variables: ' + ', '.join(sorted(leaked)))
    return environment
