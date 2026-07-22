package com.zero.xiangqi

import android.annotation.SuppressLint
import android.content.Context
import android.content.res.AssetManager
import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.MotionEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : AppCompatActivity() {

    private lateinit var glSurfaceView: GLSurfaceView

    private external fun nativeInit(assetManager: AssetManager, width: Int, height: Int)
    private external fun nativeResize(width: Int, height: Int)
    private external fun nativeRender()
    private external fun nativeTouch(action: Int, x: Float, y: Float)
    private external fun nativeToggleGameDialog(): Boolean
    private external fun nativeDestroy()

    @SuppressLint("ClickableViewAccessibility")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val assetMgr = assets

        glSurfaceView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setRenderer(object : GLSurfaceView.Renderer {
                // ✅ 新增标志位，推迟初始化
                private var isNativeInit = false

                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                    // ✅ 此时 View 尚未完成 Layout，宽高大概率为 0，切勿在此初始化
                    isNativeInit = false
                }
                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                    // ✅ 确保在拿到真实物理分辨率后再初始化 C++ 层
                    if (!isNativeInit) {
                        nativeInit(assetMgr, width, height)
                        isNativeInit = true
                    } else {
                        nativeResize(width, height)
                    }
                }
                override fun onDrawFrame(gl: GL10?) {
                    nativeRender()
                }
            })
            // 确保动画和 AI 监控连续重绘
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY

            setOnTouchListener { _, event ->
                val action = when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> 0
                    MotionEvent.ACTION_MOVE -> 2
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> 1
                    else -> -1
                }
                if (action != -1) {
                    nativeTouch(action, event.x, event.y)
                }
                true
            }
        }

        setContentView(glSurfaceView)
        window.decorView.post { hideSystemUI() }

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                nativeToggleGameDialog()
            }
        })
    }

    // 由 C++ (JNI) 调用的震动服务
    fun triggerVibrate(ms: Long) {
        // 兼容 Android 12+ 新版震动服务，消除废弃警告
        val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val vibratorManager = getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as android.os.VibratorManager
            vibratorManager.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            getSystemService(Context.VIBRATOR_SERVICE) as android.os.Vibrator
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val amplitude = if (ms >= 30) 255 else if (ms <= 15) 80 else 150
            vibrator.vibrate(VibrationEffect.createOneShot(ms, amplitude))
        } else {
            @Suppress("DEPRECATION")
            vibrator.vibrate(ms)
        }
    }

    override fun onResume() {
        super.onResume()
        glSurfaceView.onResume()
        hideSystemUI()
    }

    override fun onPause() {
        super.onPause()
        glSurfaceView.onPause()
    }

    override fun onDestroy() {
        super.onDestroy()
        nativeDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUI()
    }

    private fun hideSystemUI() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
            )
        }
    }

    companion object {
        init { System.loadLibrary("nativecpp") }
    }
}
