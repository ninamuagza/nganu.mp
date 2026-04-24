/*
 *  raymob License (MIT)
 *
 *  Copyright (c) 2023-2024 Le Juez Victor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package com.raylib.raymob;

import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.inputmethod.InputMethodManager;
import android.app.NativeActivity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Rect;
import android.os.Build;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.TextView;

public class SoftKeyboard {

    private final Context context;
    private final NativeActivity activity;
    private final InputMethodManager imm;
    private KeyEvent lastKeyEvent = null;
    private int pendingDeleteCount = 0;
    private boolean suppressNextDeleteUp = false;
    private EditText inputField = null;
    private volatile String currentText = "";
    private volatile boolean enterPressed = false;
    private boolean suppressTextWatcher = false;

    public SoftKeyboard(Context context) {
        imm = (InputMethodManager)context.getSystemService(Context.INPUT_METHOD_SERVICE);
        this.context = context;
        this.activity = (NativeActivity)context;
        activity.runOnUiThread(this::ensureInputField);
    }

    /* PUBLIC FOR JNI (raymob.h) */

    public void showKeyboard() {
        activity.runOnUiThread(() -> {
            ensureInputField();
            if (inputField == null) return;
            inputField.requestFocus();
            inputField.setSelection(inputField.getText().length());
            imm.showSoftInput(inputField, InputMethodManager.SHOW_IMPLICIT);
        });
    }

    public void hideKeyboard() {
        activity.runOnUiThread(() -> {
            if (inputField == null) return;
            imm.hideSoftInputFromWindow(inputField.getWindowToken(), 0);
            inputField.clearFocus();
        });
    }

    public int getKeyboardHeight() {
        View decorView = ((NativeActivity)context).getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsets insets = decorView.getRootWindowInsets();
            if (insets == null || !insets.isVisible(WindowInsets.Type.ime())) {
                return 0;
            }
            return Math.max(0, insets.getInsets(WindowInsets.Type.ime()).bottom);
        }

        Rect visibleFrame = new Rect();
        decorView.getWindowVisibleDisplayFrame(visibleFrame);
        int rootHeight = decorView.getRootView().getHeight();
        int bottomInset = Math.max(0, rootHeight - visibleFrame.bottom);
        int keyboardThreshold = Math.max(80, rootHeight / 10);
        return bottomInset > keyboardThreshold ? bottomInset : 0;
    }

    public boolean isKeyboardVisible() {
        View decorView = ((NativeActivity)context).getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsets insets = decorView.getRootWindowInsets();
            return insets != null && insets.isVisible(WindowInsets.Type.ime());
        }
        return getKeyboardHeight() > 0;
    }

    public int getLastKeyCode() {
        if (pendingDeleteCount > 0) return KeyEvent.KEYCODE_DEL;
        if (lastKeyEvent != null) return lastKeyEvent.getKeyCode();
        return 0;
    }

    public char getLastKeyLabel() {
        if (pendingDeleteCount > 0) return '\b';
        if (lastKeyEvent != null) return lastKeyEvent.getDisplayLabel();
        return '\0';
    }

    public int getLastKeyUnicode() {
        if (pendingDeleteCount > 0) return 0;
        if (lastKeyEvent != null) return lastKeyEvent.getUnicodeChar();
        return 0;
    }

    public void clearLastKeyEvent() {
        if (pendingDeleteCount > 0) {
            pendingDeleteCount--;
            if (pendingDeleteCount > 0) return;
        }
        lastKeyEvent = null;
    }

    public String getText() {
        return currentText;
    }

    public void setText(String value) {
        final String nextValue = value != null ? value : "";
        currentText = nextValue;
        activity.runOnUiThread(() -> {
            ensureInputField();
            if (inputField == null) return;
            String activeValue = inputField.getText().toString();
            if (activeValue.equals(nextValue)) return;
            suppressTextWatcher = true;
            inputField.setText(nextValue);
            inputField.setSelection(inputField.getText().length());
            suppressTextWatcher = false;
        });
    }

    public boolean consumeEnterPressed() {
        if (!enterPressed) return false;
        enterPressed = false;
        return true;
    }

    /* PRIVATE FOR JNI (raymob.h) */

    public void onKeyDownEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_DEL) {
            pendingDeleteCount++;
            suppressNextDeleteUp = true;
            lastKeyEvent = event;
        }
    }

    public void onKeyUpEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_DEL && suppressNextDeleteUp) {
            suppressNextDeleteUp = false;
            return;
        }
        lastKeyEvent = event;
    }

    private void ensureInputField() {
        if (inputField != null) return;

        EditText field = new EditText(context);
        field.setSingleLine(true);
        field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
        field.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        field.setBackground(null);
        field.setTextColor(Color.TRANSPARENT);
        field.setHighlightColor(Color.TRANSPARENT);
        field.setCursorVisible(false);
        field.setFocusable(true);
        field.setFocusableInTouchMode(true);
        field.setAlpha(0.01f);
        field.setLayoutParams(new FrameLayout.LayoutParams(1, 1, Gravity.START | Gravity.TOP));
        field.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) { }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) { }

            @Override
            public void afterTextChanged(Editable s) {
                if (!suppressTextWatcher) {
                    currentText = s.toString();
                }
            }
        });
        field.setOnEditorActionListener((TextView v, int actionId, KeyEvent event) -> {
            boolean handledAction = actionId == EditorInfo.IME_ACTION_DONE
                || actionId == EditorInfo.IME_ACTION_GO
                || actionId == EditorInfo.IME_ACTION_SEND
                || actionId == EditorInfo.IME_ACTION_NEXT;
            boolean handledEnter = event != null
                && event.getKeyCode() == KeyEvent.KEYCODE_ENTER
                && event.getAction() == KeyEvent.ACTION_UP;
            if (handledAction || handledEnter) {
                enterPressed = true;
                return true;
            }
            return false;
        });
        field.setOnKeyListener((View v, int keyCode, KeyEvent event) -> {
            if (keyCode == KeyEvent.KEYCODE_ENTER && event.getAction() == KeyEvent.ACTION_UP) {
                enterPressed = true;
                return true;
            }
            return false;
        });

        View decorView = activity.getWindow().getDecorView();
        if (decorView instanceof ViewGroup) {
            ((ViewGroup)decorView).addView(field);
            inputField = field;
            currentText = field.getText().toString();
        }
    }
}
