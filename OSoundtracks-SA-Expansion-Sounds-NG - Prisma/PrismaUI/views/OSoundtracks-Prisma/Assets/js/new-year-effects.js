// new-year-effects.js
(function() {
    let newYearInterval = null; // Variable global para el intervalo



    // Crear efectos de Año Nuevo
    function activateNewYearEffects() {
        let container = document.getElementById('new-year-effects-container');
        if (!container) {
            container = document.createElement('div');
            container.id = 'new-year-effects-container';
            document.body.appendChild(container);
            console.warn('New Year effects container created dynamically');
        }


        // Limpiar intervalo anterior si existe
        if (newYearInterval) clearInterval(newYearInterval);

        // Falling emojis logic removed per user request
        const emojis = ['🎆', '🎇', '🥂', '🎊', '🎉', '🧉', '🥳', '🕺', '🎐', '🎑', '🎎', '🎏', '🎍', '🎌', '🏮', '🎭'];
        newYearInterval = setInterval(() => {
            const emoji = document.createElement('div');
            emoji.textContent = emojis[Math.floor(Math.random() * emojis.length)];
            emoji.style.position = 'absolute';
            emoji.style.left = Math.random() * 100 + 'vw';
            emoji.style.fontSize = (Math.random() * 2 + 1) + 'rem';
            emoji.style.opacity = '0.2';
            emoji.style.filter = 'blur(1px)';
            emoji.style.animation = 'new-year-fall 10s linear forwards';
            container.appendChild(emoji);

            setTimeout(() => emoji.remove(), 10000);
        }, 1000);

        // Agregar emoji fijo de Año Nuevo en esquina inferior derecha
        const fixedNewYear = document.createElement('div');
        fixedNewYear.textContent = '🎆';
        fixedNewYear.style.position = 'fixed';
        fixedNewYear.style.bottom = '-10px';
        fixedNewYear.style.right = '20px';
        fixedNewYear.style.fontSize = '4rem';
        fixedNewYear.style.opacity = '0.3';
        fixedNewYear.style.filter = 'blur(0.5px)';
        fixedNewYear.style.zIndex = '1';
        fixedNewYear.style.pointerEvents = 'none';
        container.appendChild(fixedNewYear);
        // Agregar segundo emoji fijo de Año Nuevo en esquina inferior izquierda
        const fixedNewYearLeft = document.createElement('div');
        fixedNewYearLeft.textContent = '🎇';
        fixedNewYearLeft.style.position = 'fixed';
        fixedNewYearLeft.style.bottom = '-25px';
        fixedNewYearLeft.style.left = '10px';
        fixedNewYearLeft.style.fontSize = '8rem';
        fixedNewYearLeft.style.opacity = '0.3';
        fixedNewYearLeft.style.filter = 'blur(0.5px)';
        fixedNewYearLeft.style.zIndex = '1';
        fixedNewYearLeft.style.pointerEvents = 'none';
        container.appendChild(fixedNewYearLeft);


        // CSS para animación
        const style = document.createElement('style');
        style.textContent = `
            @keyframes new-year-fall {
                0% { transform: translateY(-100px) rotate(0deg); }
                100% { transform: translateY(100vh) rotate(360deg); }
            }
        `;
        document.head.appendChild(style);

        // Detener después de 5 horas para no sobrecargar
        setTimeout(() => {
            if (newYearInterval) {
                clearInterval(newYearInterval);
                newYearInterval = null;
            }
        }, 18000000);
    }

    // Función para detener efectos
    function stopNewYearEffects() {
        if (newYearInterval) {
            clearInterval(newYearInterval);
            newYearInterval = null;
        }
        const container = document.getElementById('new-year-effects-container');
        if (container) {
            container.innerHTML = ''; // Limpiar emojis existentes
        }
    }

    // Exponer funciones globales
    window.activateNewYearEffects = activateNewYearEffects;
    window.stopNewYearEffects = stopNewYearEffects;
})();