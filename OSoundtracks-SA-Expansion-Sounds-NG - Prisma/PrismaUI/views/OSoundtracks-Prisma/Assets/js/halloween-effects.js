// halloween-effects.js
(function() {
    let halloweenInterval = null; // Variable global para el intervalo



    // Crear efectos de Halloween
    function activateHalloweenEffects() {
        let container = document.getElementById('halloween-effects-container');
        if (!container) {
            container = document.createElement('div');
            container.id = 'halloween-effects-container';
            document.body.appendChild(container);
            console.warn('Halloween effects container created dynamically');
        }


        // Limpiar intervalo anterior si existe
        if (halloweenInterval) clearInterval(halloweenInterval);

        // Falling emojis logic removed per user request
        const emojis = ['🎃', '👻', '🕷️', '🕸️', '🦇', '💀', '👽', '🧛', '🧟', '🧙', '🧌', '🕯️', '🔮', '📜', '🕰️', '🌑', '🌒'];
        halloweenInterval = setInterval(() => {
            const emoji = document.createElement('div');
            emoji.textContent = emojis[Math.floor(Math.random() * emojis.length)];
            emoji.style.position = 'absolute';
            emoji.style.left = Math.random() * 100 + 'vw';
            emoji.style.fontSize = (Math.random() * 2 + 1) + 'rem';
            emoji.style.opacity = '0.2';
            emoji.style.filter = 'blur(1px)';
            emoji.style.animation = 'halloween-fall 10s linear forwards';
            container.appendChild(emoji);

            setTimeout(() => emoji.remove(), 10000);
        }, 1000);

        // Agregar emoji fijo de Halloween en esquina inferior derecha
        const fixedHalloween = document.createElement('div');
        fixedHalloween.textContent = '🎃';
        fixedHalloween.style.position = 'fixed';
        fixedHalloween.style.bottom = '-10px';
        fixedHalloween.style.right = '20px';
        fixedHalloween.style.fontSize = '4rem';
        fixedHalloween.style.opacity = '0.3';
        fixedHalloween.style.filter = 'blur(0.5px)';
        fixedHalloween.style.zIndex = '1';
        fixedHalloween.style.pointerEvents = 'none';
        container.appendChild(fixedHalloween);
        // Agregar segundo emoji fijo de Halloween en esquina inferior izquierda
        const fixedHalloweenLeft = document.createElement('div');
        fixedHalloweenLeft.textContent = '👻';
        fixedHalloweenLeft.style.position = 'fixed';
        fixedHalloweenLeft.style.bottom = '-25px';
        fixedHalloweenLeft.style.left = '10px';
        fixedHalloweenLeft.style.fontSize = '8rem';
        fixedHalloweenLeft.style.opacity = '0.3';
        fixedHalloweenLeft.style.filter = 'blur(0.5px)';
        fixedHalloweenLeft.style.zIndex = '1';
        fixedHalloweenLeft.style.pointerEvents = 'none';
        container.appendChild(fixedHalloweenLeft);


        // CSS para animación
        const style = document.createElement('style');
        style.textContent = `
            @keyframes halloween-fall {
                0% { transform: translateY(-100px) rotate(0deg); }
                100% { transform: translateY(100vh) rotate(360deg); }
            }
        `;
        document.head.appendChild(style);

        // Detener después de 5 horas para no sobrecargar
        setTimeout(() => {
            if (halloweenInterval) {
                clearInterval(halloweenInterval);
                halloweenInterval = null;
            }
        }, 18000000);
    }

    // Función para detener efectos
    function stopHalloweenEffects() {
        if (halloweenInterval) {
            clearInterval(halloweenInterval);
            halloweenInterval = null;
        }
        const container = document.getElementById('halloween-effects-container');
        if (container) {
            container.innerHTML = ''; // Limpiar emojis existentes
        }
    }

    // Exponer funciones globales
    window.activateHalloweenEffects = activateHalloweenEffects;
    window.stopHalloweenEffects = stopHalloweenEffects;
})();