# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import pytest

from HlbGmyTool.Bindings.Bindings import ValueBinding
from HlbGmyTool.Bindings.Translators import (
    FormattingError,
    QuickTranslator,
    ValidationError,
)
from HlbGmyTool.Controller.PlacedIoletController import PlacedIoletListController
from HlbGmyTool.Model.Iolets import ObservableListOfIolets


def test_quick_translator_converts_declared_input_errors():
    translator = QuickTranslator(
        lambda value: int(value),
        lambda value: float(value),
        forward_errors=(ValueError,),
        backward_errors=(ValueError, TypeError),
    )

    with pytest.raises(FormattingError, match="invalid literal") as forward_error:
        translator.Translate("bad")
    assert isinstance(forward_error.value.__cause__, ValueError)

    with pytest.raises(ValidationError) as backward_error:
        translator.Untranslate(None)
    assert isinstance(backward_error.value.__cause__, TypeError)


def test_quick_translator_leaves_undeclared_errors_visible():
    def fail(_value):
        raise RuntimeError("converter failed")

    translator = QuickTranslator(fail, fail, forward_errors=(ValueError,))

    with pytest.raises(RuntimeError, match="converter failed"):
        translator.Translate(1)
    with pytest.raises(RuntimeError, match="converter failed"):
        translator.Untranslate(1)


def test_quick_translator_does_not_assume_value_error_is_user_input():
    def fail(_value):
        raise ValueError("internal bug")

    with pytest.raises(ValueError, match="internal bug"):
        QuickTranslator(fail, lambda value: value).Translate(1)


def test_placed_iolet_translator_rejects_invalid_iolet():
    controller = PlacedIoletListController(ObservableListOfIolets())

    with pytest.raises(FormattingError) as error:
        controller.translator.Translate(object())
    assert isinstance(error.value.__cause__, ValueError)


def test_binding_displays_validation_reason_and_restores_value(capsys):
    class Source:
        value = "bad"

        def Get(self):
            return translator.Untranslate(self.value)

        def Set(self, value):
            self.value = value

    class Destination:
        key = "temperature"

        def Get(self):
            return 273.0

        def Set(self, value):
            raise AssertionError("invalid input reached the model")

    translator = QuickTranslator(
        lambda value: value,
        float,
        backward_errors=(ValueError,),
    )
    source = Source()
    destination = Destination()
    binding = object.__new__(ValueBinding)
    binding.modelMapper = destination

    binding.Sync(source, destination)

    assert source.value == 273.0
    assert "temperature" in capsys.readouterr().out
