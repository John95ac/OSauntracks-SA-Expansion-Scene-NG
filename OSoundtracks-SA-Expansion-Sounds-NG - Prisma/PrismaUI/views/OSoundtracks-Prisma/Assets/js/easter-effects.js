// easter-effects.js
(function() {
    let easterInterval = null; // Variable global para el intervalo



    // Crear efectos de Pascua
    function activateEasterEffects() {
        let container = document.getElementById('easter-effects-container');
        if (!container) {
            container = document.createElement('div');
            container.id = 'easter-effects-container';
            document.body.appendChild(container);
            console.warn('Easter effects container created dynamically');
        }


        // Limpiar intervalo anterior si existe
        if (easterInterval) clearInterval(easterInterval);

        // Falling emojis logic removed per user request
        const emojis = ['🐣', '🥚', '🐰', '🐇', '🐥', '🐦', '🍫'];
        easterInterval = setInterval(() => {
            const emoji = document.createElement('div');
            emoji.textContent = emojis[Math.floor(Math.random() * emojis.length)];
            emoji.style.position = 'absolute';
            emoji.style.left = Math.random() * 100 + 'vw';
            emoji.style.fontSize = (Math.random() * 2 + 1) + 'rem';
            emoji.style.opacity = '0.2';
            emoji.style.filter = 'blur(1px)';
            emoji.style.animation = 'easter-fall 10s linear forwards';
            container.appendChild(emoji);

            setTimeout(() => emoji.remove(), 10000);
        }, 1000);

        // Agregar emoji fijo de Pascua en esquina inferior derecha
        const fixedEaster = document.createElement('div');
        fixedEaster.textContent = '🥚';
        fixedEaster.style.position = 'fixed';
        fixedEaster.style.bottom = '-10px';
        fixedEaster.style.right = '20px';
        fixedEaster.style.fontSize = '4rem';
        fixedEaster.style.opacity = '0.3';
        fixedEaster.style.filter = 'blur(0.5px)';
        fixedEaster.style.zIndex = '1';
        fixedEaster.style.pointerEvents = 'none';
        container.appendChild(fixedEaster);
        // Agregar segundo emoji fijo de Pascua en esquina inferior izquierda
        const fixedEasterLeft = document.createElement('div');
        fixedEasterLeft.textContent = '🍫';
        fixedEasterLeft.style.position = 'fixed';
        fixedEasterLeft.style.bottom = '-25px';
        fixedEasterLeft.style.left = '10px';
        fixedEasterLeft.style.fontSize = '8rem';
        fixedEasterLeft.style.opacity = '0.3';
        fixedEasterLeft.style.filter = 'blur(0.5px)';
        fixedEasterLeft.style.zIndex = '1';
        fixedEasterLeft.style.pointerEvents = 'none';
        container.appendChild(fixedEasterLeft);


        // CSS para animación
        const style = document.createElement('style');
        style.textContent = `
            @keyframes easter-fall {
                0% { transform: translateY(-100px) rotate(0deg); }
                100% { transform: translateY(100vh) rotate(360deg); }
            }
        `;
        document.head.appendChild(style);

        // Detener después de 5 horas para no sobrecargar
        setTimeout(() => {
            if (easterInterval) {
                clearInterval(easterInterval);
                easterInterval = null;
            }
        }, 18000000);
    }

    // Función para detener efectos
    function stopEasterEffects() {
        if (easterInterval) {
            clearInterval(easterInterval);
            easterInterval = null;
        }
        const container = document.getElementById('easter-effects-container');
        if (container) {
            container.innerHTML = ''; // Limpiar emojis existentes
        }
    }

    // Exponer funciones globales
    window.activateEasterEffects = activateEasterEffects;
    window.stopEasterEffects = stopEasterEffects;
})();